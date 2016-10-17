import socket
import time
import sys

def conv( msg="CSS40 FW40 123 REQUEST FNUM\n", ip="192.168.2.46", port=5750 ):
	soc = socket.socket( socket.AF_INET, socket.SOCK_STREAM )
	
	
	HOST = socket.gethostbyname(ip)

	soc.connect( ( HOST, int( port ) ) )
	soc.send(msg)
	char =''
	resp = ''
	while ( char != '\n' ):
	
		char = soc.recv( 1 )
		resp+=char
	soc.close()
	return resp
	


class fw:

	def getall(self):
		vals = conv().split()[3:]
		return {'home':int( vals[0] ), 'com':float( vals[1] ), 'cur':float( vals[2]) }
	def go(self):
		return conv("CSS40 FW40 123 COMMAND MOVE\n")

	def stop( self ):
		return conv("CSS40 FW40 123 COMMAND STOP\n")

	def goto( self, num ):
		return conv("CSS40 FW40 123 COMMAND GOTO {0}\n".format(num) )
	
