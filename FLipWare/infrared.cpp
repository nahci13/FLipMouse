/*
     FLipWare - AsTeRICS Academy 2016
     For more info please visit: http://www.asterics-academy.net

     Module: infrared.cpp - implementation of the IR command support

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation.
  
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; See the GNU General Public License: 
   http://www.gnu.org/licenses/gpl-3.0.en.html

*/




#include "FlipWare.h"

//Time until the record command will be canceled
#define IR_USER_TIMEOUT_MS 10000
//Maximum count of edges for one command
//Note: this value may be increased if your recorded command exceeds this value
#define IR_EDGE_REC_MAX 250
//minimum count of signal edges which are necessary to accept a command
#define IR_EDGE_REC_MIN 5
//maximum number of retries, if a signal record fails (edges < IR_EDGE_REC_MIN)
#define IR_MAX_RETRIES 3

extern uint8_t IR_SENSOR_PIN;
extern uint8_t IR_LED_PIN;
extern uint8_t DebugOutput;

uint32_t edge_timeout = 10000UL;  // timeout for IR code edge length in microseconds

//current edge count
uint8_t edges;
//array of the time difference between the edges
uint16_t timings[IR_EDGE_REC_MAX];

/**
 * Record an infrared remote command with a given name.
 * 
 * This method records one infrared command to the EEPROM
 * memory. It blocks either until a command is received or
 * the user defined timeout (IR_USER_TIMEOUT_MS) occurs.
 * */
void record_IR_command(char * name)
{
	uint32_t now = 0;
	uint32_t prev = 0;
	
	uint32_t duration;
	uint8_t i;
	uint8_t toggle = 1;
	uint8_t wait = 1;
	
	//retry loop, it is ended at the last statement
	for(uint8_t retry=IR_MAX_RETRIES; retry>=0; retry--)
	{
		toggle = 1;
		//activate wait flag
		wait = 1;
		//fetch current time as previous value the first time
		prev = millis(); 
		while(wait)	//wait for start bit or user timeout of 10s
		{	
			now = millis();
			duration = now - prev; //calculate the difference
			
			//if it took longer than the user timeout (a few seconds)
			//cancel the record
			if(duration >= IR_USER_TIMEOUT_MS)
			{
				Serial.println(F("IR_TIMEOUT: User timeout"));
				return;
			} 
			//check if something happens on the IR input pin
			else if(!digitalRead(IR_SENSOR_PIN))
			{
				if(DebugOutput == DEBUG_FULLOUTPUT) { Serial.println(F("IR: Start condition")); }
				//cancel while loop by clearing wait flag
				wait = 0;
			}
		}
		
		//start the record...
		
		//record for a maximum of IR_EDGE_REC_MAX edges
		for(i=0; i<IR_EDGE_REC_MAX; i++)
		{
			//save the micros time
			prev = micros();
			//set the wait flag
			wait = 1;
			//wait until the input pin is toggled or an edge timeout occurs
			while(wait)	
			{
				//check the current duration
				now = micros();
				duration = now - prev;
				
				//is the edge timeout over?
				if(duration >= edge_timeout) 
				{
					if(DebugOutput == DEBUG_FULLOUTPUT) { Serial.println(F("IR_TIMEOUT: Edge timeout")); }
					//cancel the wait loop
					wait = 0;
					//cancel the edge record loop and save the current edges
					edges = i;
					i = IR_EDGE_REC_MAX;
				}
				//do we have a toggled pin?
				else if(digitalRead(IR_SENSOR_PIN) == toggle)
				{
					//cancel the loop
					wait = 0;
				}
			}	

			if (i<IR_EDGE_REC_MAX) 
			   timings[i] = (uint16_t)(now - prev); 
			//new edge detection
			toggle = !toggle;
		}

		//if there are enough edges, break the retry loop
		if(edges > IR_EDGE_REC_MIN) break;
		//do we reach the last retry?
		if(retry == 1)
		{
			//send a message and return (no command is stored)
			Serial.println(F("IR_TIMEOUT: Retries reached"));
			return;
		}
	}
	
	//save the recorded command to the EEPROM storage
	saveIRToEEPROM(name,timings,edges);
	
	//play a feedback tone
	makeTone(TONE_IR,0);
	
	//full edge feedback, if full debug is enabled
	if(DebugOutput == DEBUG_FULLOUTPUT)
	{
		Serial.println("START IR ----------");
		for(uint8_t i = 0; i<edges; i++)
		{
			Serial.println(timings[i]);
		}
		Serial.println("END ----------");
	}
	
	//return the recorded command name and the edge count
	Serial.print(F("IR: recorded "));
	Serial.print(name);
	Serial.print("/");
	Serial.println(edges);
}

/**
 * list all available (stored) IR commands, also parsed by the GUI
 * */
void list_IR_commands() 
{ 
	listIRCommands();
}

/**
 * Delete one IR command identified by a given name.
 * If you want to clear all IR commands, use '\0' as name
 * */
void delete_IR_command(char * name)
{
	deleteIRCommand(name);
}

/**
 * Play a previously recorded infrared remote command with a given name.
 *
 * */
void play_IR_command(char * name)
{
	uint32_t edge_now = 0;
	uint32_t edge_prev = 0;	
	uint32_t duration = 0;
	uint8_t i;
	uint32_t state_time;
	boolean output_state = HIGH;

	//fetch the IR command from the eeprom
	edges = readIRFromEEPROM(name,timings,IR_EDGE_REC_MAX);
	
	//no edges, no command -> cancel
	if(edges == 0)
	{
		if(DebugOutput == DEBUG_FULLOUTPUT) Serial.println(F("No IR command found"));
		return;
	}
	
	//full edge feedback, if full debug is enabled
	if(DebugOutput == DEBUG_FULLOUTPUT)
	{
		Serial.println("START IR ----------");
		for(uint8_t i = 0; i<edges; i++)
		{
			Serial.println(timings[i]);
		}
		Serial.println("END ----------");
	}
	
	//iterate all edges
	for(i=0; i<edges; i++)
	{
		state_time = timings[i];
		//save the current micros time
		edge_prev = micros();
		//toggle the output state
		//HIGH: IR burst
		//LOW: no IR burst
		if(output_state == HIGH)
		{
			analogWrite(IR_LED_PIN, 128);	//activate burst (PWM with 50% duty cycle)
			//wait until the next edge occurs
			do
			{
				edge_now = micros();
				duration = edge_now - edge_prev;
			} while(duration <= state_time);
			//deactivate PWM
			analogWrite(IR_LED_PIN, 0);		
			//toggle to low state
			output_state = LOW;
		}
		else
		{
			//deactivate LED
			digitalWrite(IR_LED_PIN,LOW);
			//wait until the next edge occurs
			do
			{
				edge_now = micros();
				duration = edge_now - edge_prev;
			} while(duration <= state_time);
			//toggle to high state
			output_state = HIGH;
		}

	}
	
	//play a feedback sound
	makeTone(TONE_IR,0);
	
	if(DebugOutput == DEBUG_FULLOUTPUT) 
	{
		Serial.print(F("IR: play "));
		Serial.print(name);
		Serial.print("/");
		Serial.println(IRName);
	}
	
	//infrared LED must be turned of after this function
	digitalWrite(IR_LED_PIN,LOW);	
}


void set_IR_timeout(uint16_t tout_ms)
{
   edge_timeout = (uint32_t)tout_ms * 1000;
}


