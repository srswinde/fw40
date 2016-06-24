from serial import Serial

class ardserial(Serial):
	def __init__( self ):
		Serial.__init__( self, '/dev/ttyUSB0', 9600)
		self.flush()
		
	def readline( self ):
		c=''
		buff =""
		while ( c != '\r' and c != '\n'):
			c=self.read()
			buff+=c
			
		buff=buff.replace('\r', '')
		buff=buff.replace('\n', '')
		
		
		return buff
		
		


comm = ardserial()

while 1:

	resp = comm.readline()
	if resp:
		print resp
