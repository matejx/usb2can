import wx,threading,serial,queue

class SerCanThread(threading.Thread):
#------------------------------------------------------------------------------
    def __init__(self, serp, canbr, que, evh, eid):
        threading.Thread.__init__(self)
        self.terminate = threading.Event()
        self.terminate.clear()
        self.serp = serp
        self.que = que
        self.evh = evh
        self.eid = eid
        cbrmap = {10:'S0',20:'S1',50:'S2',100:'S3',125:'S4',250:'S5',500:'S6',800:'S7',1000:'S8'}
        self.canbr = cbrmap[canbr]
#------------------------------------------------------------------------------
    def run(self):
        ser = serial.Serial(self.serp, 115200, timeout=1)
        self.ser = ser

        self.wr('C\r')
        self.wr(self.canbr+'\r')
        self.wr('O\r')

        while not self.terminate.isSet():
            s = ''
            while not self.terminate.isSet():
                c = ser.read(1).decode()
                if c == '\r': break
                s = s + c
            if len(s) == 0: continue
            if s[0] != 't': continue
            s = s[1:].rstrip()
            if (len(s) < 3+1) or (len(s) > 3+1+2*8): continue
            dlc = int(s[3])
            if len(s) != 4+2*dlc: continue
            #print(s)
            self.que.put(s)
            if self.eid > 0:
                wx.PostEvent(self.evh, wx.PyEvent(0, self.eid))

        ser.close()
#------------------------------------------------------------------------------
    def wr(self, s):
        self.ser.write(s.encode('ascii', 'ignore'))
        self.ser.write(('\r').encode('ascii', 'ignore'))
        print(s)
#------------------------------------------------------------------------------
    def join(self, timeout=None):
        self.terminate.set()
        threading.Thread.join(self, timeout)
#------------------------------------------------------------------------------
