import socket
import select
import json
import time


class JsonProtocolHandler:
    def __init__(self, sock):
        self.sock = sock
        self.peerEnded = False
        self.weEnded = False
        self.incommingBuf = b''
        self.readable = None
        self.writable = None
        self.exceptional = None
        self.attempts = 0

    def sendReq(self, id, data, showInLog=True):
        if self.weEnded:
            return
        jobj = {"id": id, "type": "req", "data": data}
        stosend = json.dumps(jobj, separators=(',', ':'))
        if showInLog:
            print(stosend)
        self.sock.send(stosend.encode("utf-8"))

    def sendAns(self, id, data, showInLog=True):
        if self.weEnded:
            return
        jobj = {"id": id, "type": "ans", "data": data}
        stosend = json.dumps(jobj, separators=(',', ':'))
        if showInLog:
            print(stosend)
        self.sock.send(stosend.encode("utf-8"))

    def sendVer(self, ver):
        if self.weEnded:
            return
        jobj = {"id": 0, "type": "ver", "version": ver}
        stosend = json.dumps(jobj, separators=(',', ':'))
        self.sock.send(stosend.encode("utf-8"))

    def end(self,  force=False):
        if self.weEnded and not force:
            return
        jobj = {"id": 0, "type": "end"}
        stosend = json.dumps(jobj, separators=(',', ':'))
        self.sock.send(stosend.encode("utf-8"))
        self.weEnded = True
        if self.peerEnded or force:
            self.sock.shutdown(2)
            self.sock.close()

    def reqArrived(self, id, data):
        print("REQ {:d}:".format(id))
        print(data)

    def ansArrived(self, id, data):
        print("ANS {:d}:".format(id))
        print(data)

    def processBuffer(self):
        try:
            strbuf = self.incommingBuf.decode("utf-8")
        except:
            return False
        if len(strbuf) == 0:
            return
        idx = 0
        for cc in strbuf:
            if cc == '{':
                if idx > 0:
                    strbuf = strbuf[idx:]
                    idx = 0
                break
            idx += 1
        if idx > 0:
            self.incommingBuf = strbuf.encode("utf-8")
            return False
        in_string = False
        in_esc = False
        brace_nesting_level = 0
        i = 0
        while i < len(strbuf):
            curr_ch = strbuf[i]
            if curr_ch == '"' and not in_esc:
                in_string = not in_string
                i += 1
                continue
            if not in_string:
                if curr_ch == '{':
                    brace_nesting_level += 1
                elif curr_ch == '}':
                    brace_nesting_level -= 1
                    if brace_nesting_level == 0:
                        sdoc = strbuf[:i+1]
                        if len(strbuf) == i + 1:
                            strbuf = ""
                        else:
                            strbuf = strbuf[i+1:]
                        self.incommingBuf = strbuf.encode("utf-8")
                        i = -1
                        in_string = False
                        in_esc = False
                        brace_nesting_level = 0
                        try:
                            jdoc = json.loads(sdoc)
                        except ValueError as err:
                            print("malformed json...")
                            jdoc = None
                        if jdoc is not None and len(jdoc) > 0:
                            if "id" in jdoc and "type" in jdoc:
                                if jdoc["type"] == "end":
                                    print("END received")
                                    self.peerEnded = True
                                    if self.weEnded:
                                        self.sock.shutdown(2)
                                        self.sock.close()
                                    return True
                                if jdoc["type"] == "ver":
                                    print("VER received")
                                    self.sendVer(1)
                                    return True
                                if jdoc["type"] == "ans" or jdoc["type"] == "req":
                                    data = jdoc["data"]
                                    if jdoc["type"] == "ans":
                                        self.ansArrived(jdoc["id"], data)
                                    else:
                                        self.reqArrived(jdoc["id"], data)
                                    return True
                        i += 1
                        continue
            else:
                if curr_ch == '\\' and not in_esc:
                    in_esc = True
                else:
                    in_esc = False
            i += 1
        return False

    def readyRead(self):
        if self.weEnded:
            return False
        inputs = [self.sock]
        outputs = []
        try:
            self.readable, self.writable, self.exceptional = select.select(inputs, outputs, inputs, 1)
        except select.error:
            self.sock.shutdown(2)
            self.sock.close()
            self.weEnded = True
            self.peerEnded = True
        if self.weEnded or self.peerEnded:
            return False
        if self.sock not in self.readable and sock not in self.exceptional:
            return False
        ndata = b''
        try:
            ndata = self.sock.recv(1024)
        except socket.error:
            pass
        if ndata == b'' and self.attempts < 10:
            self.attempts += 1
            return False
        self.attempts = 0
        if not self.peerEnded:
            self.incommingBuf += ndata
            if self.processBuffer():
                self.attempts = 10
            return True
        return False


class QuikConnectorTest(JsonProtocolHandler):
    def __init__(self, sock):
        super().__init__(sock)
        self.msgId = 0
        self.sayHelloMsgId = None
        self.msgWasSent = False
        self.clsListReqId = None
        self.clsList = None
        self.createDsReqId = None
        self.ds = None
        self.setUpdCBReqId = None
        self.updCBInstalled = False
        self.updCnt = 0
        self.closeDsMsgId = None

    def nextStep(self):
        if not self.msgWasSent:
            self.sayHello()
            self.msgWasSent = True
        else:
            if self.clsList is None:
                if self.clsListReqId is None:
                    self.getClassesList()
            elif self.ds is None:
                if self.createDsReqId is None:
                    self.createDs()
            elif not self.updCBInstalled:
                self.setDsUpdateCallback()
            elif self.updCnt >= 10:
                if self.closeDsMsgId is None:
                    self.closeDs()

    def reqArrived(self, id, data):
        super().reqArrived(id, data)
        if data["method"] == 'invoke':
            if data["function"] == 'sberUpdated':
                idx = data["arguments"][0]
                ans = {"method": "return", "result": self.sberUpdated(idx)}
                self.sendAns(id, ans)


    def ansArrived(self, id, data):
        super().ansArrived(id, data)
        if id == self.clsListReqId:
            self.clsList = data['result'][0].split(",")
            self.clsList = list(filter(None, self.clsList))
            print("Received", len(self.clsList), "classes")
        elif id == self.createDsReqId:
            self.ds = data['result'][0]
        elif id == self.closeDsMsgId:
            self.end()
        elif id == self.sayHelloMsgId:
            print("hello sent")
        elif id == self.setUpdCBReqId:
            print("update handler installed")
        else:
            self.updCnt += 1
            # self.end()

    def sayHello(self):
        self.msgId += 1
        self.sendReq(self.msgId, {"method": "invoke", "function": "PrintDbgStr", "arguments": ["Hello from python!"]})
        # self.sendReq(self.msgId, {"method": "invoke", "function": "message", "arguments": ["Hello from python!", 1]})
        self.sayHelloMsgId = self.msgId

    def getClassesList(self):
        self.msgId += 1
        self.sendReq(self.msgId, {"method": "invoke", "function": "getClassesList", "arguments": []})
        self.clsListReqId = self.msgId

    def createDs(self):
        self.msgId += 1
        self.sendReq(self.msgId, {"method": "invoke", "function": "CreateDataSource", "arguments": ["TQBR", "SBER", 5]})
        self.createDsReqId = self.msgId

    def setDsUpdateCallback(self):
        self.msgId += 1
        self.sendReq(self.msgId, {"method": "invoke", "object": self.ds, "function": "SetUpdateCallback", "arguments": [{"type": "callable", "function": "sberUpdated"}]})
        self.setUpdCBReqId = self.msgId
        self.updCBInstalled = True

    def closeDs(self):
        self.msgId += 1
        self.sendReq(self.msgId, {"method": "invoke", "object": self.ds, "function": "Close", "arguments": []})
        self.closeDsMsgId = self.msgId

    def sberUpdated(self, index):
        print("sberUpdated:", index)
        req = {"method": "invoke", "object": self.ds, "function": "C", "arguments": [index]}
        self.msgId += 1
        self.sendReq(self.msgId, req)
        return True


# Create a TCP/IP socket
sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

# sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
# Connect the socket to the port where the server is listening
server_address = ('localhost', 57777)
# server_address = ('10.211.55.21', 62787)
# server_address = ('37.193.88.181', 57578)
print('connecting to %s port %d' % server_address)
sock.connect(server_address)

phandler = QuikConnectorTest(sock)

sock.setblocking(0)
while not phandler.weEnded:
    rrRes = phandler.readyRead()
    if not rrRes:
        phandler.nextStep()

print("finished")
