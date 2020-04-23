//#include <openssl/rand.h>
#include <string>
#include <iostream>


#include "Party.h"
#include "TcpClient.h"
#include "TcpServer.h"
#include "Helper.h"

#define SUCCESS 1

using std::string;
using std::cout;
using std::endl;


Party::Party(short myID,long input):_id(myID),_input(input){
	//expend the vector to contain all parties' sockets
	this->_sockets.resize(NUM_OF_PARTIES);
	this->_msgs.resize(NUM_OF_PARTIES);
	this->_keys.resize(NUM_OF_PARTIES);
	//this->_mtx.resize(NUM_OF_PARTIES);
	for (int i = 0; i < NUM_OF_PARTIES; i++) {
		if (i == _id)
			continue;
		_msgs[i] = new Message;
	}
	//AutoSeededRandomPool rnd;
	//_keys[_id] = new SecByteBlock(0x00,KEY_LEN);
	//rnd.GenerateBlock(*_keys[_id],_keys[_id]->size());
	//cout<<std::dec << endl;

}
void Party::printKey(unsigned short index)const {
	if (index < 0 || index > NUM_OF_PARTIES)
		return;
	string a((char*)_keys[index]->begin(), KEY_LEN);
	for (int i = 0; i < a.length(); i++)
		printf("%u ", (byte)a[i]);
}
void Party::connectToAllParties(string IPs[NUM_OF_PARTIES]) {
	bool isConnected = false;
	//compute the party's id this party needs to initiate communication and the party's id this party needs to wait for a connection
	unsigned short idToConnect = (_id + 1) % NUM_OF_PARTIES;
	unsigned short idFromConnect = (_id + 2) % NUM_OF_PARTIES;//-1 mod 3 = 2

	//as mentioned abobe only with IPs
	string toIP = Helper::IPCompare(IPs[1], IPs[2]) ^ (_id % 2 == 0) ? IPs[1] : IPs[2];
	string myIP = IPs[0];
	
	//toPort - 6200[id + 1] myPort - 62000 - [id]
	unsigned short toPort = BASE_PORT + idToConnect, myPort = BASE_PORT + _id;
	
	//setup a server socket 
	TcpServer* from =new TcpServer(myPort);
	this->_sockets[idFromConnect] = from;
	from->serve(_msgs[idFromConnect]);// , _mtx[idFromConnect]);
	TRACE("Waiting for clients..");

	//setup a client socket
	TcpClient* to = new TcpClient(myIP, myPort, toPort, toIP, _msgs[idToConnect],isConnected);// , _mtx[idToConnect]);
	this->_sockets[idToConnect] = to;

	//check that the sockect to the other parties were created succssesfully
	while (!from->isValid());
	while (!isConnected);

	TRACE("Succssesfully connected to all parties!\n");

}
void Party::broadcast(byte* msg,unsigned short messageType)const {
	for (int i = 0; i < NUM_OF_PARTIES ; i++) {
		if (i == _id)
			continue;
		sendTo(i, messageType, msg);
	}
}
Party::~Party() {
	//delete all the sockets of the party
	while (_sockets.size()) {
		TcpSocket* toFree = _sockets.back();
		//safety check before using delete
		if (toFree) {
			delete toFree;
			toFree = nullptr;
		}
		_sockets.pop_back();
	}
	//delete all the sockets of the party
	while (_msgs.size()) {
		Message* toFree = _msgs.back();
		//safety check before using delete
		if (toFree) {
			delete toFree;
			toFree = nullptr;
		}
		_msgs.pop_back();
	}
	//delete all the sockets of the party
	while (_keys.size()) {
		SecByteBlock* toFree = _keys.back();
		//safety check before using delete
		if (toFree) {
			delete toFree;
			toFree = nullptr;
		}
		_keys.pop_back();
	}
}
bool Party::sendTo(unsigned short id, unsigned short messageType, byte* msg)const {
	Message toSend(messageType);
	toSend.setData(msg);
	_sockets[id]->writeBuffer(&toSend, HEADER_SIZE);
	_sockets[id]->writeBuffer(toSend.getData(), toSend.getSize());
	return true;
}
void Party::readFrom(unsigned short id,unsigned char* msg) {
	while (this->_msgs[id]->getIsRead());
	memcpy(msg,_msgs[id]->getData(),_msgs[id]->getSize());
	//update thread that the message was read
	this->_msgs[id]->setIsRead(true);
}
unsigned short Party::getID()const { return this->_id; }
void Party::calcSeq() {
	AutoSeededRandomPool rnd;
	byte seqMy[SEQ_LEN];
	byte seqTo[SEQ_LEN];
	byte seqFrom[SEQ_LEN];

	//genereting random seq
	rnd.GenerateBlock(seqMy, SEQ_LEN);

	//broadcast seq to other parties
	broadcast(seqMy, SEQ);

	readFrom((_id + 1) % NUM_OF_PARTIES, seqTo);
	readFrom((_id + 2) % NUM_OF_PARTIES, seqFrom);

	*(unsigned int*)_finalSeq = *(unsigned int*)seqFrom + *(unsigned int*)seqMy + *(unsigned int*)seqTo;

	TRACE("SEQ = %u", *(unsigned int*)_finalSeq);
}
Share* Party::fRand() {
	static unsigned int calledThisFunc = 0;
	Share* ans = new Share(_id,'a'+calledThisFunc);
	byte alpha[NUM_OF_PARTIES][KEY_LEN];//TODO: conver to Share!
	byte fromKey[KEY_LEN];
	byte IV[KEY_LEN];
	AutoSeededRandomPool rnd;
	calledThisFunc++;
	_keys[_id] = new SecByteBlock(0x00,KEY_LEN);
	rnd.GenerateBlock(*_keys[_id],_keys[_id]->size());
	for (int i = 0; i < KEY_LEN / SEQ_LEN; i++)
		*(unsigned int*)(IV+i * SEQ_LEN) = *(unsigned int*)_finalSeq;

	//send this party key to the next party
	sendTo((_id + 1) % NUM_OF_PARTIES,KEY, _keys[_id]->data());
	readFrom((_id + 2) % NUM_OF_PARTIES, fromKey);

	_keys[(_id + 2) % NUM_OF_PARTIES] = new SecByteBlock(fromKey,KEY_LEN);

	for (int i = 0; i < NUM_OF_PARTIES; i++) {
		if (i == (_id + 1)%NUM_OF_PARTIES)
			continue;
		memcpy_s(alpha[i], sizeof(int), &_finalSeq, sizeof(int));
		Helper::encryptAES(alpha[i], KEY_LEN,*_keys[i],IV); 
		(*ans)[i] = *(long*)alpha[i];
		//TRACE("Alpha %d:%u", i, *(unsigned int*)alpha[i]);
	}
	//free vector of keys
	for (int i = 0; i < NUM_OF_PARTIES; i++) {
		if (!_keys[i])
			continue;
		delete _keys[i];
		_keys[i] = nullptr;
	}
	return ans;
}
void Party::fInput() {
	vector<Share*> randomShares;
	long randomNum;
	byte partiesInputs[NUM_OF_PARTIES][ENC_INPUT_LEN];
	randomShares.resize(NUM_OF_PARTIES);
	calcSeq();
	for (int i = 0; i < NUM_OF_PARTIES; i++) {
		randomShares[i] = fRand();//randomShares[i] - the random number for input #i
		cout << "Share #" << i << " " << randomShares[i]->toString() << endl;
	}
	randomNum = reconstruct(*randomShares[_id]);
	randomNum = _input - randomNum;
	broadcast((byte*)&randomNum,ENC_INPUT);

	readFrom((_id + 2) % NUM_OF_PARTIES, partiesInputs[(_id + 2) % NUM_OF_PARTIES]);
	readFrom((_id + 1) % NUM_OF_PARTIES, partiesInputs[(_id + 1) % NUM_OF_PARTIES]);

}
long Party::reconstruct(Share& myShare) {
	byte name = myShare[_id].getName();
	byte rawData[NUM_OF_PARTIES][RECONSTRUCT_LEN];//the answers from the other parties
	byte sendShare[RECONSTRUCT_LEN];
	vector<Share*> otherShares;

	otherShares.resize(NUM_OF_PARTIES);
	unsigned short smallerId = (_id + 2) % NUM_OF_PARTIES == NUM_OF_PARTIES-1?_id: (_id + 2) % NUM_OF_PARTIES;
	unsigned short biggerId = !((_id + 2) % NUM_OF_PARTIES == NUM_OF_PARTIES - 1) ? _id : (_id + 2) % NUM_OF_PARTIES;

	*(unsigned short*)sendShare= myShare[smallerId].getIndex();
	*(long*)(sendShare+2) = myShare[smallerId].getValue();
	sendShare[10] = myShare[smallerId].getName();

	*(unsigned short*)(sendShare+ 11) = myShare[biggerId].getIndex();
	*(long*)(sendShare+13) = myShare[biggerId].getValue();
	sendShare[21] = myShare[biggerId].getName();

	broadcast(sendShare, RECONSTRUCT);
	//read answers from other parties
	for (int i = NUM_OF_PARTIES - 1; i >=0; i--) {
		if (i == _id) {
			otherShares[i] = nullptr;										  //(2B,8B,1B)X2
			continue;
		}
		readFrom((_id + 2 + i) % NUM_OF_PARTIES, rawData[i]);//(index,value,name),(index,value,name)
		otherShares[i] = new Share(*(unsigned short*)rawData[i] + 1, rawData[i][10]);
		(*otherShares[i])[(i + 2) % NUM_OF_PARTIES] = *(long*)(rawData[i] + 2);//put the value recievied in the share
		(*otherShares[i])[i] = *(long*)(rawData[i] + 13);//put the value recievied in the share
	}

	//perform validity check that the answers we got 
	for (int i = 0; i < NUM_OF_PARTIES; i++) {
		if(i == (_id + 1) % NUM_OF_PARTIES)
			if((*otherShares[(i+2)%NUM_OF_PARTIES])[(_id + 1) % NUM_OF_PARTIES].getValue() != (*otherShares[(i + 1) % NUM_OF_PARTIES])[(_id + 1) % NUM_OF_PARTIES].getValue())
				throw std::exception(__FUNCTION__ "I'm surrounded by liers!");
		if (myShare[(i + 2) % NUM_OF_PARTIES].getValue() != (*otherShares[(i + 2) % NUM_OF_PARTIES])[(i + 2) % NUM_OF_PARTIES].getValue() )
			throw std::exception(__FUNCTION__  "I'm surrounded by liers!");
	}
	long ans = myShare[(_id + 2) % NUM_OF_PARTIES].getValue() + myShare[_id].getValue() + (*otherShares[(_id + 1) % NUM_OF_PARTIES])[(_id + 1) % NUM_OF_PARTIES].getValue();
	for (int i = 0; i < NUM_OF_PARTIES; i++)
		if (otherShares[i]) {
			delete otherShares[i];
			otherShares[i] = nullptr;
		}
	return ans;
}
