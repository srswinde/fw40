import socket
import time
import sys

def conv( msg="VATT FLAT 123 REQUEST GETALL\n", ip="192.168.2.46", port=5750 ):
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
		return conv("VATT FLAT 123 COMMAND MOVE\n")

	def stop( self ):
		return conv("VATT FLAT 123 COMMAND STOP\n")

	def pulse( self, pulse ):
		return conv("VATT FLAT 123 COMMAND PULSE {0}\n".format(pulse) )
	def goto( self, num ):
		return conv("VATT FLAT 123 COMMAND GOTO {0}\n".format(num) )


def getdata():
	f = fw()
	f.go()
	f.pulse( 4 )
	home = 0
	t0=time.time()
	while( 1 ):
		time.sleep(0.001)
		ALL = f.getall()

		home, com, cur = ALL["home"], ALL['com'], ALL['cur']
		print time.time() - t0, home, com, cur

	f.stop()

def main(filt):
	f = fw()
	print f.goto(int(filt))

if __name__ == "__main__":
	if len(sys.argv) > 1:
		main(sys.argv[1])
