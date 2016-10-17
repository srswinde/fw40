import socket
import time
import sys

"""Simple python interface to the 40" fitler wheel."""

def conv( msg="CSS40 FW40 123 REQUEST FNUM\n", ip="192.168.2.46", port=5750 ):
	"""
		Handle the back meat of the socket connection
	"""
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
	"""Class to create easy interface to the css40 filter wheel. It uses ng protocol
	and a short set of requests and commands"""
	def __init__(self, obsid="CSS40", sysid="FW40", ip='192.168.2.46', port=5750 ):
		self.obsid = obsid
		self.sysid = sysid
		self.ip = ip
		self.port = port
		self.refnum = 0

	def command( self, cmdname , arglist=[]):
		"""Send a command to the filter wheel"""
		if type(arglist) in (str, int, float):
			arglist = [arglist]
		elif type(arglist) == list:
			pass
		else:
			raise TypeError( 'Arglist must be a string int or float or a list of these types.' )
		
		argstr = ""
		for arg in arglist:
			argstr = argstr + str( arg ) +' '
		cmdstr = "{} {} {} COMMAND {} {}\n".format( self.obsid, self.sysid, self.refnum, cmdname, argstr)
		self.refnum = (self.refnum+1) % 999
		return conv( cmdstr, self.ip, self.port  )

	def request(self, reqname, arglist=[] ):
		"""Send a request to the filter wheel"""
		if type(arglist) in (str, int, float):
			arglist = [arglist]
		elif type(arglist) == list:
			pass
		else:
			raise TypeError( 'Arglist must be a string int or float or a list of these types.' )
		
		argstr = ""

		for arg in arglist:
			argstr = argstr + str( arg ) +' '
		cmdstr = "{} {} {} REQUEST {} {}\n".format( self.obsid, self.sysid, self.refnum, reqname, argstr)
		self.refnum = (self.refnum+1) % 999
		return conv( cmdstr, self.ip, self.port  )
		
	def reqMOVE(self):
		"""Move the filter wheel 1 filter over"""
		return self.command("MOVE")

	def comSTOP( self ):
		"""Stop all motion"""
		return self.command("STOP")

	def comGOTO( self, num ):
		"""Go to the specified filter by number"""
		return self.command( "GOTO", num )
	
	def reqMOT( self ):
		"""Returns the motion status"""
		return self.request("MOT")

	def reqFNUM(self):
		"""Returns the current filter number"""
		return self.request('FNUM')
	
