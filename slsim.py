#!/usr/bin/python

import sys,serial

ser = serial.Serial('COM6', 115200, timeout=0.5)

try:
  while True:
    s = ''
    while True:
      t = ser.read(1)
      if (t == '')or(t == '\r')or(t == '\n'): break
      s = s + t
    
    s = s.rstrip();
    
    if len(s) == 0:
      continue
    
    print '<' + s
    
    r = '???'

    if s[0] == 'v':
      r = 'v0100\r'
    
    if s[0] == 'V':
      r = 'V0100\r'
    
    if s[0] == 'C':
      r = '\x07'

    if s[0] == 'S':
      r = '\r'

    if s[0] == 'Z':
      r = '\r'
    
    if s[0] == 'L':
      r = '\r'

    print '>' + r.replace('\r','_').replace('\x07','-')
    ser.write(r)

finally:
  ser.close()
