#include <SPI.h>
#include <Ethernet.h>

// #include "NGPROTO.h"
// network configuration.  gateway and subnet are optional.

#define OBSID "CSS40"
#define SYSID "FW40"

#define NO_ERROR 0
#define OBSID_ERROR 1
#define SYSID_ERROR 2
#define REFNUM_ERROR 3
#define QUERY_ERROR 4

#define RAMPUP 1
#define RAMPDOWN 0


const char * ecode[] = {
		"NO ERROR",
		"OBSID ERROR",
		"SYSID ERROR",
		"REFNUM ERROR",
		"QUERY ERROR"
};

#define LED 9
#define STEPPIN 5 //Orange wire
#define DIRPIN 6  //Blue wire
#define ENABPIN 7 //Brown wire

#define ADVPIN 8

#define HOMEPIN 4

#define ENCPINA 2
#define ENCPINB 3

//Some short cuts to set the prescaler bit.
#define PRESC_1 (1 << CS10)
#define PRESC_8  (1 << CS11)
#define PRESC_64 (1 << CS11) | (1 << CS10)
#define PRESC_256 (1 << CS12)
#define PRESC_1024 (1 << CS12) | (1 << CS10)


//Globals 
char fnames[6][50];
/*
strcpy ( fnames[0], "FILTER0" );
strcpy ( fnames[1], "FILTER1" );
strcpy ( fnames[2], "FILTER2" );
strcpy ( fnames[3], "FILTER3" );
strcpy ( fnames[4], "FILTER4" );
strcpy ( fnames[5], "FILTER5" );

*/

//Encoder states. The state variable probably doesn't have to be global
short state = 0;
short lastState = 0;

//Encoder position and commanded position
double pos = 0; //
double comPos = 6*360;

//Ramp variables. 
double rampDist = 10.0;
double rampUpCount = 0;
double rampUpPos = 0; 

//These Intervals control the pulse width
//and therefore the speed of the motor
unsigned int maxInterval = 20;
unsigned int minInterval = 0;

//pulseInterval determines the speed.
//This value is linearly interpolated
//between the min and max intervals
//over ramping.
unsigned int pulseCount = 0;
unsigned int pulseInterval = maxInterval;

//distance after first home sense to home
int maxHomeCount = 20;
int homeCount = -1000;



//interrupt frequence
int freq = 4000;//in hz

//motion state
bool motion;
bool lmotion = false;//last motion



double encRes = 0.9;


//pulsing state
bool pulse = false;

//homing state home on startup
bool homing = true;


//array of possible encoder states
const short diffStates[4][4] = {
                                  {0, 1, -1,  2},
                                  {-1, 0, 2, 1},
                                  {1, 2, 0, -1},
                                  {2, -1, 1, 0}
                                  };

//This is how many counts we have moved
//as a function of each the diffStates
const short diffPos[4][4] = {
                              {0, 1, 2, 3},
                              {-1, 0, 1, 2},
                              {-2, -1, 0, 1}, 
                              {-3, -2, -1, 0}
                                };
float dp = 0;//difference between commanded and current



 // the media access control (ethernet hardware) address for the shield:
byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
//the IP address for the shield:
//byte ip[] = { 128, 196, 211, 162 };    
// the router's gateway address:
//byte gateway[] = { 128,196,208,1};
// the subnet:
//byte subnet[] = { 255, 255, 0, 0 };


//Mount Lemmon CSS network
byte subnet[] = { 255,255,255,0 };
byte gateway[] = { 192,168,2,1 };
byte ip[] = {192,168,2,46};


struct ng_data
{
  char sysid[20];
  char obsid[10];
  unsigned short refNum;
  char queryType[10];
  unsigned short arg_count;  
  char args[10][50];
  
};

struct ng_data data;
struct ng_data currCom;

bool homepinState=false;
bool lhomepinState=false;

unsigned char rampState = RAMPUP;

bool move = false;
double shit=0;

EthernetServer server = EthernetServer(5750);



//End Globals

//Function prototypes
int parseNG( char inRaw[] , struct ng_data *parsed );
void printNG( struct ng_data *inputd );
void handle_input( struct ng_data *ngInput, char resp[] );
double linInterp(double, double, double, double, double);


/*****************************
*	Interrupt Service Routine
*
*	Name: ISR
*	Args: TIMER1_COMPA_vect
*		WHich timer for interrupt
*
*
******************************/
ISR(TIMER1_COMPA_vect)
{
	

	if ( (comPos-pos) >= 0 )//if wheel is ahead 
		dp = comPos - pos;
	else//wheel is behind (go all the way around)
		dp = 6*360 + ( comPos-pos );
		
	
	
	//clip pulse width at min or max.
	if( pulseInterval > maxInterval )
		pulseInterval = maxInterval;
	else if( pulseInterval < minInterval )
		pulseInterval = minInterval;
	
	
   if( pulseCount > pulseInterval )
   {//Send a step to the driver.
		pulse = pulse^1;
		digitalWrite( STEPPIN, pulse );
		pulseCount = 0;
		
	}
	
	if( dp > encRes )
	{//We are not where we need to be move!
		motion = true;
		pulseCount++;
	}
	else
	{//We are there. Stop.
		motion = false;

	}
	
	
	if( dp < rampDist )
	{
		rampState = RAMPDOWN;
	}
	else
	{//We just started moving
		rampState = RAMPUP;
	}
	
	//Read the encoder pins and stuff them into a 2 bit number
	state = digitalRead(ENCPINA) + (digitalRead(ENCPINB) << 1);
	
	if( state != lastState )
	{//We moved
	
		
		if ( digitalRead(HOMEPIN) == 0 )
		{
			//We see the homing magnet!
			homepinState = true;
		}
		else
		{
			homepinState = false;
		}
		
		//update position
	 	pos = pos - diffStates[state][lastState]*encRes;
	 		
		if( homepinState == false && lhomepinState == true )
		{
			pos=17.0;
			if(homing)
			{
				homing=false;
				comPos = 0;
			}
				
		}
		
		if (rampState == RAMPUP)
			rampUpCount = rampUpCount - diffStates[state][lastState]*encRes;
		else if( rampState == RAMPDOWN )
		{	shit++;
			rampUpCount = rampUpCount + diffStates[state][lastState]*encRes;
		}
		
		 	
	 	if( rampUpCount > rampDist )
	 		rampUpCount = rampDist;
	 	else if(rampUpCount < 0 )
	 		rampUpCount = 0;	
	 		
	 	//set the speed based on ramp
		pulseInterval =  (int) round( linInterp( rampUpCount, 0, maxInterval, rampDist, minInterval ) );
		
		lhomepinState = homepinState;
		lastState = state;
	}//End if(state != lastState)
	

	
	//set the speed based on ramp
	
	lmotion = motion;
	
}


//Arduino Setup function
void setup()
{
  
	// initialize the ethernet device
	Ethernet.begin(mac, ip, gateway, subnet );

	//Serial
	//Serial.begin(9600);

	//Set up various pin modes
	pinMode(LED, OUTPUT);
	pinMode( ENABPIN, OUTPUT );
	pinMode( STEPPIN, OUTPUT );

	pinMode( HOMEPIN, INPUT );
	
	pinMode( ADVPIN, INPUT_PULLUP );
	
	pinMode( ENCPINA, INPUT_PULLUP );
	pinMode( ENCPINB, INPUT_PULLUP );
	
	digitalWrite(ENABPIN, HIGH);

	// start listening for clients


	


	cli();//stop interrupts

	//setup timer1 for interrupts
	TCCR1A = 0;// set entire TCCR1A register to 0
	TCCR1B = 0;// same for TCCR1B
	TCNT1  = 0;//initialize counter value to 0
	
	
	
	// set compare match register this should give us 800hz
	OCR1A = (int) ( 16e6/ (8*freq) -1 ) ;// = ((16*10^6) / PRESC_8)*PERIOD - 1 (must be <65536)
	// turn on CTC mode
	TCCR1B |= (1 << WGM12);
	// Set CS12 and CS10 bits for 8 prescaler
	TCCR1B |= PRESC_8;
	//TCCR1B |= (1 << CS01);
	// enable timer compare interrupt
	TIMSK1 |= (1 << OCIE1A);

	//TCCR0A = 0;// set entire TCCR1A register to 0
	//TCCR0B = 0;// same for TCCR1B
	//TCNT0  = 0;//initialize counter value to 0
	OCR0B = 100;
	//TCCR0B |= (1 << WGM12);
	

	sei();//allow interrupts

	server.begin();
}


unsigned int debounce;
void loop()
{

	int charCounter= 0;
	char rawin[100];
	char clientResp[50];
	char fullResp[150];
	
	// if an incoming client connects, there will be bytes available to read:
	EthernetClient client = server.available();


	while (client.connected()) {
	 
		if (client.available())
		{
			// read bytes from the incoming client and write them back
			// to any clients connected to the server:
			char c = client.read();
			if (c == '\n')
			{
			  
				int syntaxError = parseNG(rawin, &currCom);
				
				if ( syntaxError == NO_ERROR )
				{
					
					handle_input( &currCom, clientResp );
					sprintf( fullResp, "CSS40 FWHEEL %i %s", currCom.refNum, clientResp );
					client.println( fullResp );
				}
				else
			 		client.println(ecode[syntaxError]);
			 		
				rawin[0] = '\0';

			}
			else
			{
				rawin[ charCounter ] = c;
				rawin[charCounter+1] = '\0';
				charCounter++;

			}


		}
	 
	}

	if(!motion)
	{
		if( digitalRead(ADVPIN ) == LOW )
		{
			if ( debounce-- <= 0 )
			{
				comPos = comPos+360;
				if( comPos > 2160)
					comPos = 360;
					
				debounce=20;
			}
		}
	}
	
}

double linInterp( double x, double x0, double y0, double x1, double y1 )
{//Simple Linear interpolation. For now exclusively to control ramping. 
	double m = ( y1 - y0 )/( x1 - x0 );
	double b = y0 - m*x0;
	
	return m*x+b;

}

void floater( double val, char buff[] )
{//Turn a floating point into a string for printing
    int n,f;
    char sign = '+';
    if( val < 0 )
    {
    	sign = '-';
    }
    n = (int) val;
    f = (int) ((val - (int) val)*1000);
    sprintf( buff, "%c%i.%i", sign, n,f  );

}

void handle_input( struct ng_data *ngInput, char resp[] )
{
	
	//char helper[50] ;
	strcpy( resp, "UNKOWN COMMAND" );
	int newPos;
	int fnum;
	char allstr[20];
	char str1[50] = "\0";
	char str2[50] = "\0";
	
	
	
	if ( strcmp( ngInput->queryType, "COMMAND") == 0 )
	{
    

		if( strcmp( ngInput->args[0] , "DISABLE" ) == 0 )
		{
			digitalWrite( ENABPIN, LOW);
			strcpy( resp, "ok" );
			

		}
		else if( strcmp( ngInput->args[0], "ENABLE" ) == 0)
		{
			digitalWrite( ENABPIN, HIGH);
			strcpy( resp, "ok" );

		}
		
		else if( strcmp( ngInput->args[0], "FNAME" ) == 0 )
		{

			sscanf( ngInput->args[1], "%i", &fnum);
			if(fnum >=0 || fnum < 6)
			{
				
				strcpy( fnames[fnum], ngInput->args[2] );
				strcpy( resp, "ok" );
			}
			
		}
		
		else if( strcmp( ngInput->args[0], "GOTO" ) == 0 )
		{
			
			sscanf( ngInput->args[1], "%i", &newPos );
			newPos = newPos*360;
			rampUpCount=0;
			if( (newPos >= 0) && (newPos < 1801) && ( (int) newPos % 360) == 0 )
			{
				comPos=newPos;
				
				strcpy( resp, "ok" );
			}

		}
		
		else if( strcmp( ngInput->args[0], "GOTONMAE" ) == 0 )
		{
			strcpy( resp, "???" );
			for( fnum=0; fnum < 6; fnum++ )
			{
				if( strcmp( fnames[fnum], ngInput->args[1] ) == 0 )
				{
					newPos=fnum*360;
					if( (newPos >= 0) && (newPos < 1801) && ( (int) newPos % 360) == 0 )
					{
						comPos=newPos;
						strcpy( resp, "ok" );
					}
				}
			}
			
			

		}
		
		else if( strcmp ( ngInput->args[0], "HOME" ) == 0 )
		{
			homing = true;
			rampUpCount=0;
			strcpy( resp, "ok" );

		}
		
		else if( strcmp ( ngInput->args[0], "MOVE" ) == 0 )
		{
			move=true;
			strcpy( resp, "ok" );

		}
    	else if( strcmp ( ngInput->args[0], "STOP" ) == 0 )
		{
			move=false;
			strcpy( resp, "ok" );

		}
		
		else if( strcmp ( ngInput->args[0], "PULSE" ) == 0 )
		{
			pulseInterval = atol( ngInput->args[1] );
			strcpy( resp, "ok" );

		}
    
	}
	else if( strcmp(  ngInput->queryType, "REQUEST" ) == 0 )
	{
		

		if( strcmp(  ngInput->args[0], "FNAME" ) == 0 )
		{	
				strcpy( resp, "???");
				sscanf( ngInput->args[1], "%i", &fnum );
				if( fnum >= 0 && fnum < 6)
					if( fnames[fnum] != NULL)
						strcpy( resp, fnames[fnum] ); 
				
		}
		else if( strcmp(  ngInput->args[0], "FNUM" ) == 0 )
		{
				strcpy( resp, "???" ) ;

				for( fnum=0; fnum < 6; fnum++ )
				{
					if( strcmp( fnames[fnum], ngInput->args[2] ) == 0 )
					{
						floater( fnum, str1 );
						strcpy( resp, str1 )		;
					}
				}		
		}
		
		
		else if(strcmp(  ngInput->args[0], "GETALL")== 0 )
		{	
			floater( comPos, str1 );
			floater( pos, str2 );
			sprintf(allstr, "%i %s %s %i", homepinState, str1, str2, pulseInterval );
			strcpy(resp, allstr);
		}
		
		else if(strcmp(   ngInput->args[0], "ISMOVING" )== 0 )
		{
			sprintf( resp, "%i", motion );
		}
		
		else if(strcmp(  ngInput->args[0], "READ_ENC") == 0 )
		{
			floater( pos, str1  );
			strcpy( resp, str1 );
			
		}
		
		else if(strcmp(  ngInput->args[0], "READHOME") == 0 )
		{
			sprintf( resp, "%i", digitalRead( HOMEPIN ) ) ;
		}
		else if(strcmp(  ngInput->args[0], "HOMECOUNT") == 0 )
		{
			sprintf(resp, "%i", homeCount );
		}
		else if(strcmp(  ngInput->args[0], "PULSE") == 0 )
		{
			sprintf( resp, "%i", pulseInterval );
			
		}

	}
  
  
	else
	{
		strcpy( resp, "BAD" );
	} 
	

}





int parseNG( char inRaw[] , struct ng_data *parsed )
{


  
  short word_count = 0;
  char *tok;
  
  int errorState = 0;
  
  int refNum;
  char queryType[10];
  char obsid[10];
  char sysid[10];
  

  tok = strtok(inRaw, " \t");
  
	while( tok != NULL )
	{  
		
		switch(word_count)
		{
			case 0://observation ID
				strcpy( obsid, tok );
				if( strcmp( obsid, OBSID ) == 0)
					strcpy( parsed->obsid, tok );
				else
						errorState = 1;
				break;
        
			case 1://system ID
				strcpy( sysid, tok );
				
				if( strcmp( sysid, SYSID ) == 0 )
					strcpy( parsed->sysid, tok );
				
				else
					errorState = 2;
				break;
			
			case 2://reference number
				refNum = atoi( tok );
					if( refNum > 0 )
						parsed->refNum = refNum;
					else 
					errorState = 3;
					break;
        
			case 3://query type
				strcpy( queryType, tok );
				
				if ( ( strcmp(queryType, "COMMAND") == 0) || ( strcmp(queryType, "REQUEST") == 0) )
					strcpy( parsed->queryType, queryType );
				else
					errorState = 4;
				break;

			default://Arguments
				strcpy( parsed->args[word_count - 4], tok  );
        
    }
    tok = strtok( NULL, " \t" );
    word_count++;
  }
  parsed->arg_count = word_count - 1;
  
  
  return errorState;
  
  
}
