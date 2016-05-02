#include <SPI.h>
#include <Ethernet.h>

// #include "NGPROTO.h"
// network configuration.  gateway and subnet are optional.

#define OBSID "VATT"
#define SYSID "FLAT"

#define NO_ERROR 0
#define OBSID_ERROR 1
#define SYSID_ERROR 2
#define REFNUM_ERROR 3
#define QUERY_ERROR 4

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




//Encoder states. The state variable probably doesn't have to be global
short state = 0;
short lastState = 0;

//Encoder position and commanded position
double pos = 0; //
double comPos = 0;

//Ramp variables. 
double rampDist = 20.0;
double rampUpCount = 0;
double rampUpPos = 0; 

//These Intervals control the pulse width
//and therefore the speed of the motor
unsigned int maxInterval = 3;
unsigned int minInterval = 1;

//pulseInterval determines the speed.
//This value is linearly interpolated
//between the min and max intervals
//over ramping.
unsigned int pulseCount = 0;
unsigned int pulseInterval = 0;

//distance after first home sense to home
int maxHomeCount = 20;
int homeCount = -1000;

//interrupt frequence
int freq = 4000;//in hz

//motion state
bool motion;
bool lmotion = false;//last motion


//FYI
double stepSize = 1.8/4.0;
double period = 1/freq;
double encRes = 0.9;
double vel = 0;
double accel = 1;

//pulsing state
bool pulse = false;

//homing state home on startup
bool homing = true;



//The follow are arrays of possible encoder states
const short diffStates[4][4] = {
                                  {0, 1, -1,  2},
                                  {-1, 0, 2, 1},
                                  {1, 2, 0, -1},
                                  {2, -1, 1, 0}
                                  };

//This is howman counts we have moved
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
byte ip[] = { 128, 196, 211, 162 };    
// the router's gateway address:
byte gateway[] = { 128,196,208,1};
// the subnet:
byte subnet[] = { 255, 255, 0, 0 };



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


EthernetServer server = EthernetServer(5750);

//End Globals

//Function prototypes
int parseNG( char inRaw[] , struct ng_data *parsed );
void printNG( struct ng_data *inputd );
String handle_input( struct ng_data *ngInput );
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
		
	
	//pulse width at min max.
	if(pulseInterval > maxInterval) 
		pulseInterval = maxInterval;
	else if( pulseInterval < minInterval )
		pulseInterval = minInterval;
	
	
   if( pulseCount > pulseInterval )
   {//Send a step to the driver.
		pulse = pulse^1;
		digitalWrite( STEPPIN, pulse );
		pulseCount = 0;
	}
	
	if( dp > 2*encRes || homing )
	{//We are not where we need to be move!
		motion = true;
		pulseCount++;
	}
	else
	{//We are there. Stop. 
		motion = false;
		//pos=comPos;
	}
	
	if( motion == true && lmotion == false)
	{//We just started moving
		rampUpCount = 0;
	}
	
	if( (dp < rampDist) )
	{//We are close, ramp down linearly
		pulseInterval = (int) round( linInterp( dp, rampDist, minInterval, 0, maxInterval ) );
	}
	else if( rampUpCount < rampDist )
	{//We started movign, Ramp up linearly
		pulseInterval = (int) round( linInterp( rampUpCount, 0, maxInterval, rampDist, minInterval ) );
		
		if ( (pos-rampUpPos) > 0)
		{//if pos-rampUpPos is not negative ramp up. 
			rampUpCount += (pos-rampUpPos);	
		}
		rampUpPos=pos;
	}
	else
	{// Max speed
		pulseInterval = 0;
	}
	
	
	
	//Read the encoder pins and stuff them into a 2 bit number
	state = digitalRead(ENCPINA) + (digitalRead(ENCPINB) << 1);
	if(state != lastState)
	{//WE have moved update the position
	 	pos = pos - diffStates[state][lastState]*encRes;
	 	lastState = state;
	 	
		if ( digitalRead(HOMEPIN) == 0 )
		{//We see the homing magnet!
		
			//We are almost home update the position thusly
			//This should only happen at startup.
			if (pos < 6*360-maxHomeCount && homing)
				pos=6*360.0-maxHomeCount;

			if(homing)
			{//We are close, ramp down as a function of homeCount
				pulseInterval = ( int ) linInterp(homeCount, maxHomeCount, maxInterval, 0, minInterval);
			}

			//The magnet is detected for about 40 encoder steps
		 	//We count to 20 and we will be at about the middle 
		 	// of the home position.
			if( homeCount >= maxHomeCount )
			{//we should be there. 
				pos = 0;
				
				//set homeCount to arbitrarily negative number
				homeCount = -1000;
				
				if(homing)
				{//We have homed 
					
					
					
					homing = false;
					comPos = 0;
					pos = 0;
				}
			  
			}

			homeCount++;

		}
		else
		{
				homeCount = 0;
				
		}

	}
	lmotion = motion;
	
}


//Arduino Setup function
void setup()
{
  
	// initialize the ethernet device
	Ethernet.begin(mac, ip, gateway, subnet );

	//Serial
	Serial.begin(9600);

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


	
	fnames[0][0] = NULL;
	fnames[1][0] = NULL;
	fnames[2][0] = NULL;
	fnames[3][0] = NULL;
	fnames[4][0] = NULL;
	fnames[5][0] = NULL;

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



void loop()
{

	int charCounter= 0;
	char rawin[200];
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
					client.println(handle_input( &currCom ));
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
			if ( pos >= (360*5-2*encRes) )
				comPos = 0;
			else
				comPos = pos+360;
			
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
    n = (int) val;
    f = (int) ((val - (int) val)*1000);
    sprintf( buff, "%i.%i", n,f  );

}

String handle_input( struct ng_data *ngInput )
{
	
	//char helper[50] ;
	String resp = "UNKOWN COMMAND";
	int newPos;
	int fnum;
	
	char str1[50];
	char str2[50];
	char str3[50];		
	char str4[50];
	char str5[50];
	char str6[50];
	

	if ( strcmp( ngInput->queryType, "COMMAND") == 0 )
	{
    

		if( strcmp( ngInput->args[0] , "DISABLE" ) == 0 )
		{
			digitalWrite( ENABPIN, LOW);
			resp = "ok";

		}
		else if( strcmp( ngInput->args[0], "ENABLE" ) == 0)
		{
			digitalWrite( ENABPIN, HIGH);
			resp = "ok";

		}
		
		else if( strcmp( ngInput->args[0], "FNAME" ) == 0 )
		{

			sscanf( ngInput->args[1], "%i", &fnum);
			if(fnum >=0 || fnum < 6)
			{
				
				strcpy( fnames[fnum], ngInput->args[2] );
				resp = "ok";
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
				
				resp = "ok";
			}

		}
		
		else if( strcmp( ngInput->args[0], "GOTONMAE" ) == 0 )
		{
			resp = "???";
			for( fnum=0; fnum < 6; fnum++ )
			{
				if( strcmp( fnames[fnum], ngInput->args[1] ) == 0 )
				{
					newPos=fnum*360;
					if( (newPos >= 0) && (newPos < 1801) && ( (int) newPos % 360) == 0 )
					{
						comPos=newPos;
						resp = "ok";
					}
				}
			}
			
			

		}
		
		else if( strcmp ( ngInput->args[0], "HOME" ) == 0 )
		{
			homing = true;
			rampUpCount=0;
			resp = "ok";

		}
    
    
	}
	else if( strcmp(  ngInput->queryType, "REQUEST" ) == 0 )
	{
		

		if( strcmp(  ngInput->args[0], "FNAME" ) == 0 )
		{	
				resp = "???";
				sscanf( ngInput->args[1], "%i", &fnum );
				if( fnum >= 0 && fnum < 6)
					if( fnames[fnum] != NULL)
						resp = String( fnames[fnum] );
				
		}
		else if( strcmp(  ngInput->args[0], "FNUM" ) == 0 )
		{
				resp = "???";

				for( fnum=0; fnum < 6; fnum++ )
					if( strcmp( fnames[fnum], ngInput->args[2] ) == 0 )
						resp = String( fnum );

						
		}
		
		
		else if(strcmp(  ngInput->args[0], "GETALL")== 0 )
		{

		}
		
		else if(strcmp(   ngInput->args[0], "ISMOVING" )== 0 )
		{
			resp = String(motion);
		}
		
		else if(strcmp(  ngInput->args[0], "READ_ENC") == 0 )
		{
			floater( pos, str1  );
			resp=String( str1 );
		}
		
		else if(strcmp(  ngInput->args[0], "READHOME") == 0 )
		{
			resp = String( digitalRead( HOMEPIN ) );
		}
		else if(strcmp(  ngInput->args[0], "HOMECOUNT") == 0 )
		{
			resp = String( homeCount );
		}
		

	}
  
  
	else
	{
		resp="BAD";
	} 
	
	return resp;
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
