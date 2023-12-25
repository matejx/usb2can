#!/usr/bin/python3

import sys,wx,sercanthr,queue

can_msg_desc = {
'0C6': 'BCM (immo response)',
'0C7': 'ECU (immo challenge)',
'0CF': 'BCM (lights, heating, brakes)',
'0DD': 'ECU (temp, accu, inject, rpm)',
'0E5': 'BCM (spd, fuel, tmg)',
'0F0': 'BCM (handbrake, doors)',
'0F2': 'PSCU (city)',
'0F3': 'BCM',
'700': 'BCM',
'701': 'ECU',
'703': 'BCM',
'740': 'BCM',
'743': 'BCM'
}
#------------------------------------------------------------------------------
class myframe(wx.Frame):
    def __init__(self,parent,title,serp,canbr):
        wx.Frame.__init__(self,parent,title=title,size=(500,400))
        self.Bind(wx.EVT_CLOSE, self.onClose)
        self.parent = parent
        self.que = queue.Queue() # message (string) queue
        self.mcnt = {}  # message count
        self.mlii = {}  # message list item index
        self.rxeid = 0
        self.pertx = ""

        self.list1 = wx.ListCtrl(self, -1, style=wx.LC_REPORT)
        self.list1.InsertColumn(0, 'id', width=50)
        self.list1.InsertColumn(1, 'dlc', width=30)
        self.list1.InsertColumn(2, 'data', width=120)
        self.list1.InsertColumn(3, 'count', width=50)
        self.list1.InsertColumn(4, 'info', width=200)

        self.edit1 = wx.TextCtrl(self,-1,value=u"t000100")
        self.Bind(wx.EVT_TEXT_ENTER, self.onEdit1, self.edit1)

        self.btn1 = wx.Button(self, -1, label=u"Send every 100ms")
        self.Bind(wx.EVT_BUTTON, self.onBtn1, self.btn1)

        # enable either Timer based queue polling
        self.timer1 = wx.Timer(self)
        self.Bind(wx.EVT_TIMER, self.onTimer)
        self.timer1.Start(100)
        # or by SerCanThread generated window message
        #self.rxeid = wx.NewId()
        #eb = wx.PyEventBinder(self.rxeid)
        #self.Bind(eb, self.onTimer)

        szr = wx.BoxSizer(wx.VERTICAL)
        szr.Add(self.list1, 1, wx.EXPAND)
        szr.Add(self.edit1, 0, wx.EXPAND)
        szr.Add(self.btn1, 0, wx.EXPAND)

        self.SetSizer(szr)
        self.Show(True)

        self.sct = sercanthr.SerCanThread(serp, canbr, self.que, self, self.rxeid)
        #self.sct = testcanthr.TestCanThread(self.que, self, self.rxeid)
        self.sct.start()
#------------------------------------------------------------------------------
    def onEdit1(self,event):
        self.sct.wr(self.edit1.GetValue())
        self.edit1.Clear()
#------------------------------------------------------------------------------
    def onBtn1(self,event):
        if self.pertx:
            self.pertx = ""
            self.btn1.SetLabel(u"Start")
        else:
            self.pertx = self.edit1.GetValue()
            self.btn1.SetLabel(u"Stop")
#------------------------------------------------------------------------------
    def list1Sort(self, item1, item2):
        if item1 > item2: return 1
        return -1
#------------------------------------------------------------------------------
    def list1AddCanMsg(self, can_id, can_dlc, can_data, can_info = ''):
        if can_id in self.mcnt: # if this message ID already seen
            n = self.mcnt[can_id] + 1   # increase message counter
            self.mcnt[can_id] = n
            i = self.mlii[int(can_id, 16)]  # get listitem index for this message
            if( can_data != self.list1.GetItem(i, 2).GetText() ): # update data if necessary
                self.list1.SetStringItem(i, 2, can_data)
                print(can_id,can_data)
            self.list1.SetItem(i, 3, str(n))  # update counter
        else: # this message ID seen for the first time
            print(can_id,can_data)
            self.mcnt[can_id] = 1   # set message counter to 1
            self.list1.Append((can_id, can_dlc, can_data, '1', can_info)) # create a new listitem for it
            i = self.list1.GetItemCount() - 1 # since Append doesn't return new listitem's index
            self.list1.SetItemData(i, int(can_id, 16)) # set listitem's user data for sorting
            self.list1.SortItems(self.list1Sort) # sort listctrl by message ID
            # since listitem indexes were messed up by sort, rebuild dict
            for i in range(self.list1.GetItemCount()):
                mid = self.list1.GetItemData(i)
                self.mlii[mid] = i
#------------------------------------------------------------------------------
    def onTimer(self,event):
        try:
            if self.pertx:
                self.sct.wr(self.pertx)

            while True:
                s = self.que.get(False)
                can_id = s[:3]
                can_dlc = s[3]
                can_data = s[4:]
                can_info = ''
                if can_id in can_msg_desc: can_info = can_msg_desc[can_id]
                self.list1AddCanMsg(can_id, can_dlc, can_data, can_info)
                continue # don't print decoding
                # --- decoding ---
                if can_id == '0DD':
                    self.list1AddCanMsg('1', '', s[8:12], 'accu')
                    self.list1AddCanMsg('2', '', s[12:16], 'inject')
                    self.list1AddCanMsg('3', '', s[16:18], 'rpm')
                    self.list1AddCanMsg('4', '', s[18:20], 'fuel?')
                if can_id == '0E5':
                    self.list1AddCanMsg('5', '', s[4:8], 'speed')
                    self.list1AddCanMsg('6', '', s[8:12], 'TBD')
        except queue.Empty:
            pass
        except Exception as e:
            print(str(e))
#------------------------------------------------------------------------------
    def onClose(self,event):
        print('Stopping SerCanThread...')
        self.sct.join()
        self.Destroy()
#------------------------------------------------------------------------------
if __name__ == "__main__":
    if len(sys.argv) < 3:
        print('usage: moncan.py serial_port CAN_baudrate_kHz')
        exit(1)
    app = wx.App()
    frame = myframe(None, 'MonCAN', sys.argv[1], int(sys.argv[2]))
    app.MainLoop()
