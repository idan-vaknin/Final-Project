#include "Party.h"
#include "TcpClient.h"
#include "TcpServer.h"
#include "Circuit.h"
//NTL
#define NTL_NO_MIN_MAX
#include "NTL/ZZ_pX.h"
#include "NTL/vec_ZZ.h"
#include "NTL/lzz_p.h"
#include "NTL/tools.h"

#include <string>
#include <iostream>
#include <assert.h>
#include <condition_variable>

#define SUCCESS 1
#define POWER_TWO_MAX_RANGE 9
//std
using std::string;
using std::cout;
using std::endl;
//NTL
using NTL::ZZ_p;
using NTL::GenPrime;
using NTL::ZZ_pX;
using NTL::vec_ZZ_p;
using NTL::interpolate;
using NTL::RandomBnd;
using NTL::rep;
using NTL::vec_ZZ;
using NTL::VectorRandom;
using NTL::random;
using NTL::ZZ;
using NTL::vec_ZZ_pX;

Party::Party(short myID,long input):_id(myID),_input(input),_arithmeticCircuit(nullptr){
	ZZ p(ZP);
	//for Dbug
	 srand(10);
	//GenPrime(p, POWER_TWO_MAX_RANGE);
	NTL::ZZ_p::init(p);
	cout << "P="<<p;
	//make sure the input belong to the Ring Z_p 
	 _input %= ZP;
	//expend the vector to contain all parties' sockets
	this->_sockets.resize(NUM_OF_PARTIES);
	this->_msgs.resize(NUM_OF_PARTIES);
	this->_keys.resize(NUM_OF_PARTIES);
	this->_shares.resize(NUM_OF_PARTIES);

	//this->_mtx.resize(NUM_OF_PARTIES);
	for (int i = 0; i < NUM_OF_PARTIES; i++) {
		if (i == _id)
			continue;
		_msgs[i] = new Message;
	}

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
void Party::broadcast(byte* msg, byte messageType)const {
	for (unsigned short i = 0; i < NUM_OF_PARTIES ; i++) {
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
			//delete toFree; HEAP CORRUPTION-to check:maybe change to delete[]
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
bool Party::sendTo(unsigned short id, byte messageType, byte* msg)const {
	Message toSend(messageType);
	if(messageType == F_VERIFY_ROUND1_MESSAGE)
		toSend.setSize(messageType, (_arithmeticCircuit->getNumOfMulGates()/L*2+6*L+1)*sizeof(ZZ_p));
	else
		toSend.setSize(messageType);
	toSend.setData(msg);
	_sockets[id]->writeBuffer(&toSend, HEADER_SIZE);
	_sockets[id]->writeBuffer(toSend.getData(), toSend.getSize());
	return true;
}
void Party::readFrom(unsigned short id,byte* msg) {
	std::condition_variable& other = _msgs[id]->getListenerCV();
	std::condition_variable& mine = _msgs[id]->getPartyCV();
	std::mutex& m = _msgs[id]->getIsReadMutex();
	std::unique_lock<std::mutex> partyUL(m);
	//wait until mutex is unlocked or for a new message to be received
	mine.wait(partyUL, [&] {return !this->_msgs[id]->getIsRead(); });
	memcpy(msg,_msgs[id]->getData(),_msgs[id]->getSize());
	other.notify_one();
	//mark message as read
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
	calledThisFunc++;
	
	byte alpha[NUM_OF_PARTIES][KEY_LEN]{};//TODO: conver to Share!
	byte fromKey[KEY_LEN]{};
	byte IV[KEY_LEN]{};
	AutoSeededRandomPool rnd;
	Share* ans = new Share((_id + 2) % NUM_OF_PARTIES, 'a' + calledThisFunc);
	
	_keys[_id] = new SecByteBlock(0x00,KEY_LEN);
	rnd.GenerateBlock(*_keys[_id],_keys[_id]->size());
	
	for (unsigned int i = 0; i < KEY_LEN / SEQ_LEN; i++)
		*(unsigned int*)(IV+i * SEQ_LEN) = *(unsigned int*)_finalSeq;
		//*(unsigned int*)(IV + i * SEQ_LEN) = *(unsigned int*)_finalSeq;

	//send this party key to the next party
	sendTo((_id + 1) % NUM_OF_PARTIES,KEY, _keys[_id]->data());
	readFrom((_id + 2) % NUM_OF_PARTIES, fromKey);

	_keys[(_id + 2) % NUM_OF_PARTIES] = new SecByteBlock(fromKey,KEY_LEN);

	for (unsigned short i = 0; i < NUM_OF_PARTIES; i++) {
		if (i == (_id + 1)%NUM_OF_PARTIES)
			continue;
		memcpy_s(alpha[i], sizeof(int), &_finalSeq, sizeof(int));
		Helper::encryptAES(alpha[i], KEY_LEN,*_keys[i],IV); 
		(*(long*)alpha[i]) %= ZP;
		(*ans)[i] = (*(long*)alpha[i])>0?*(long*)alpha[i]: (*(long*)alpha[i])+ZP;
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
		//cout << "Share #" << i << " " << randomShares[i]->toString() << endl;
	}
	//reconstruct the random value of this party
	randomNum = reconstruct(randomShares);
	randomNum = _input + (ZP - randomNum);
	randomNum %= ZP;
	//brodcast the salted input
	broadcast((byte*)&randomNum,ENC_INPUT);
	//reciecve other parties salted inputs
	readFrom((_id + 2) % NUM_OF_PARTIES, partiesInputs[(_id + 2) % NUM_OF_PARTIES]);
	readFrom((_id + 1) % NUM_OF_PARTIES, partiesInputs[(_id + 1) % NUM_OF_PARTIES]);
	memcpy_s(partiesInputs+_id, sizeof(long), &randomNum, sizeof(long));

	for (int i = 0; i < NUM_OF_PARTIES; i++) {
		_shares[i] = new Share(*randomShares[i] + (*(long*)partiesInputs[i]));
	}
	//build circuit
	_arithmeticCircuit = new Circuit(_finalSeq, this);

	this->_gGatesInputs.resize(_arithmeticCircuit->getNumOfMulGates()* INPUTS_PER_MUL_GATE);

	//release the memory which was allocated in fRand
	for (int i = 0; i < NUM_OF_PARTIES; i++) {
		delete randomShares[i];
	}
	////convert the share recieved from the other parties to Share and add it to the vector _shares
	//for (unsigned short i = 0; i < NUM_OF_PARTIES; i++) {
	//	Share* receivedShare = new Share((i+2)%NUM_OF_PARTIES, 'a' + i);
	//	(*receivedShare)[i].setValue(*(unsigned long*)partiesInputs[i]);
	//	(*receivedShare)[(i+2)%NUM_OF_PARTIES].setValue(*(unsigned long*)partiesInputs[i]);////fucked
	//	_shares[i] = receivedShare;
	//}
}
long Party::finalReconstruct(Share& myShare) {
	vector<Share*> outputShares;
	long ans = 0;
	outputShares.resize(NUM_OF_PARTIES);

	//send the final output share to the other parties
	for (int i = 0; i < NUM_OF_PARTIES; i++) {
		if (i != _id) {
			sendShareTo(i, myShare);
		}
	}
	//reciecve the shares of the final output
	for (int i = 0; i < NUM_OF_PARTIES; i++) {
		if (i != _id)
			outputShares[i] = &RecieveShareFrom(i);
		else
			outputShares[i] = &myShare;
	}

	//sum all shares to get result
	for (int i = 0; i < NUM_OF_PARTIES; i++)
		ans += (*outputShares[i])[i].getValue();
	ans %= ZP;
	return ans;
}
void Party::sendShareTo(unsigned short id, Share& toSend)const {
	byte sendShare[RECONSTRUCT_LEN];
	*(unsigned short*)sendShare = toSend[(_id + 2) % NUM_OF_PARTIES].getIndex();
	*(long*)(sendShare + 2) = toSend[(_id + 2) % NUM_OF_PARTIES].getValue();
	sendShare[6] = toSend[(_id + 2) % NUM_OF_PARTIES].getName();

	*(unsigned short*)(sendShare + 7) = toSend[_id].getIndex();
	*(long*)(sendShare + 9) = toSend[_id].getValue();
	sendShare[13] = toSend[_id].getName();
	sendTo(id, RECONSTRUCT, sendShare);
}
Share& Party::RecieveShareFrom(unsigned short id) {
	byte rawData[RECONSTRUCT_LEN]{};//the answers from the other parties
	readFrom(id, rawData);//(index,value,name)
	Share& ans = *new Share(*(unsigned short*)rawData, rawData[6]);
	ans[(id + 2) % NUM_OF_PARTIES] = *(long*)(rawData + 2);//put the value recievied in the share
	ans[id] = *(long*)(rawData+ 9);//put the value recievied in the share
	return ans;
}
long Party::reconstruct(vector<Share*>& shares) {
	byte name = (*shares[_id])[_id].getName();
	vector<Share*> otherShares;

	otherShares.resize(NUM_OF_PARTIES);
	//
	for (unsigned short i = 0; i < NUM_OF_PARTIES; i++) {
		if (i == _id)
			continue;
		sendShareTo(i, *shares[i]);
	}

	//read answers from other parties
	for (unsigned short i = 0; i <NUM_OF_PARTIES; i++) {
		if (i == _id) {
			otherShares[i] = nullptr;			
			continue;
		}
		otherShares[i] = &RecieveShareFrom(i);
	}
	bool isValid = false;
	switch (_id) {
	case 0:
		isValid = (*otherShares[1])[1].getValue() == (*otherShares[2])[1].getValue() && (*shares[_id])[0].getValue() == (*otherShares[1])[0].getValue() && (*shares[_id])[2].getValue() == (*otherShares[2])[2].getValue();
		break;
	case 1:
		isValid = (*otherShares[2])[2].getValue() == (*otherShares[0])[2].getValue() && (*shares[_id])[0].getValue() == (*otherShares[0])[0].getValue() && (*shares[_id])[1].getValue() == (*otherShares[2])[1].getValue();
		break;
	case 2:
		isValid = (*otherShares[1])[0].getValue() == (*otherShares[0])[0].getValue() && (*shares[_id])[1].getValue() == (*otherShares[1])[1].getValue() && (*shares[_id])[2].getValue() == (*otherShares[0])[2].getValue();
		break;
	}
	if (! isValid)
		throw std::exception(__FUNCTION__  "I'm surrounded by liers!");

	//perform validity check that the answers we got 
	/*for (int i = (_id+2)%NUM_OF_PARTIES, j = 0;j <=NUM_OF_PARTIES; j++,i++) {
		if (i  == (_id + 1) % NUM_OF_PARTIES) {
			if ((*otherShares[(_id + 2) % NUM_OF_PARTIES])[i].getValue() != (*otherShares[(_id + 1) % NUM_OF_PARTIES])[i].getValue())
				throw std::exception(__FUNCTION__ "I'm surrounded by liers!");
			continue;
		}
		if((*otherShares[_id])[i].getValue() != (*otherShares[(i+j)%NUM_OF_PARTIES])[i].getValue())
			throw std::exception(__FUNCTION__ "I'm surrounded by liers!");	
	}*/
	long ans = (*shares[_id])[(_id + 2) % NUM_OF_PARTIES].getValue() + (*shares[_id])[_id].getValue() + (*otherShares[(_id + 1) % NUM_OF_PARTIES])[(_id + 1) % NUM_OF_PARTIES].getValue();
	ans %= ZP;
	for (int i = 0; i < NUM_OF_PARTIES; i++)
		if (otherShares[i]) {
			delete otherShares[i];
			otherShares[i] = nullptr;
		}
	return ans;
}

Share* Party::getShare(int index) {
	if (index < NUM_OF_PARTIES) {
		return _shares[index];
	}
	throw std::exception(__FUNCTION__ "Index is invalid!");
}

void Party::setShare(Share* share, int index) {
	if (index < NUM_OF_PARTIES) {
		_shares[index] = share;
		return;
	}
	throw std::exception(__FUNCTION__ "Index is invalid!");
}
Share* Party::calcCircuit(){
	return _arithmeticCircuit->getOutput();
}
void Party::setG_GateInput(unsigned short index, Part value) {
	assert(index >= 0 && index < _arithmeticCircuit->getNumOfMulGates()* INPUTS_PER_MUL_GATE);
	this->_gGatesInputs[index] = value;
}
Circuit* Party::getArithmeticCircuit()const {
	return _arithmeticCircuit;
}
void Party::fVerify() {
	unsigned int M = _arithmeticCircuit->getNumOfMulGates() / L;//as descussed in the pepare
	ZZ_pX p;
	vector<ZZ_pX> inputPolynomials;
	inputPolynomials.resize(6 * L);
	//-----Round 1-----:
	verifyRound1(M,inputPolynomials,p);
	//-----Round 2-----:
	verifyRound2(M, inputPolynomials,p);
	//-----Round 3-----:
	verifyRound3();
	//----------------------------------------------------------------------------------
	//real_1d_array x = "[0,1,2,3,4,5,6,7,8,9]";
	//real_1d_array y = "[0,0,2,6,12,20,30,42,56,72]";
	//barycentricinterpolant p;
	//real_1d_array a;
	//double v;

	//// barycentric model is created
	//polynomialbuild(x, y, p);
	//v = barycentriccalc(p, 10);
	////
 // // a=[0,-1,+1] is decomposition of y=x^2-x in the power basis:
 // //
 // //     y = 0 - 1*x + 1*x^2
 // //
 // // We convert it to the barycentric form.
 // //
	//polynomialbar2pow(p, a);
	//for (int i = 0; i < 10; i++)
	//	if (i != 9)
	//		std::cout <<"("<< a[i] << ")x^" << i << "+";
	//	else
	//		std::cout << "(" << a[i] << ")x^" << i;



}
void Party::verifyRound1(unsigned int M, vector<ZZ_pX>& inputPolynomials,ZZ_pX& p) {
	//(a)
	int numOfElements = L;
	vector<ZZ_p> thetas;
	generateRandomElements(thetas, numOfElements);
	//(b)
	vec_ZZ_p omegas;
	omegas.SetLength(6 * L);
	for(int i=0;i<6*L;i++)
		random(omegas[i]);
	//(c)
	vector<vec_ZZ_p> pointsToInterpolate;
	pointsToInterpolate.resize(6 * L);
	interpolateInputPolynomials(M, pointsToInterpolate, omegas, inputPolynomials);
	for (int i = 0; i < 6*L; i++)
		std::cout<<"f(" << i << ")" << inputPolynomials[i] << std::endl;
	//(d)
	p.SetLength(2 * M + 1);
	for (int i = 0; i < 6 * L; i++)
		p += inputPolynomials[i];
	std::cout << "p(x) = " << p << std::endl;
	//(e)
	vec_ZZ_p PI;
	PI.SetLength(2 * M + 1 + 6 * L);
	
	AutoSeededRandomPool rnd;
	unsigned long long* nextPI = (unsigned long long*)new SecByteBlock(0x00, (2 * M + 1 + 6 * L) * sizeof(ZZ_p));
	vec_ZZ_p beforePI;
	beforePI.SetLength(2 * M + 1 + 6 * L);

	//add 6*L omegas to f
	for (int i = 0; i < 6 * L; i++) {
		PI[i] = omegas[i];
	}
	//add 2*M + 1 coeficients to f
	for (int i = 0; i < 2 * M+1; i++) {
		PI[i+6*L] = p[i];
		cout << PI[i+6 * L] << " ";
	}
	for (int i = 0; i < 2 * M + 1 + 6 * L; i++) {
		beforePI[i] = PI[i] - ZZ_p(*(unsigned long long*)(&nextPI[i]));
	}
	//send PI_+1
	sendTo((_id + 1) % NUM_OF_PARTIES, F_VERIFY_ROUND1_MESSAGE, (byte*)nextPI);
	byte* toSend = new byte[(2 * M + 6 * L + 1)*sizeof(ZZ_p)]{};
	ZZ bytesToZZ;
	for (int i = 0; i < 2 * M+6*L+1; i++) {
		byte rawZp[sizeof(ZZ_p)]{};
		BytesFromZZ(rawZp, rep(beforePI[i]), sizeof(ZZ_p));
		NTL::ZZFromBytes(bytesToZZ, rawZp, sizeof(ZZ_p));
		cout << "bytesToZZ:" << bytesToZZ << endl;
		cout << "rawZp:" << (unsigned long long)*rawZp << endl;
		cout << "beforePI["<<i<<"]:" << beforePI[i] << endl;
		*(unsigned long long*)&toSend[i*sizeof(ZZ_p)] = *(unsigned long long*)rawZp;
	}
	sendTo((_id + 2) % NUM_OF_PARTIES, F_VERIFY_ROUND1_MESSAGE,toSend);

	//-------------------release memory section-------------------
	thetas.clear();
	thetas.shrink_to_fit();

	delete nextPI;
	nextPI = nullptr;
	
	delete toSend;
	toSend = nullptr;
}
void Party::verifyRound2(unsigned int M, vector<ZZ_pX>& inputPolynomials, ZZ_pX& p) {
	byte* PIs[NUM_OF_PARTIES];
	for (int i = 0; i < NUM_OF_PARTIES; i++)
		if (i == _id) {
			PIs[i] = nullptr;
			continue;
		}
		else
			PIs[i]=new byte(sizeof(unsigned long long) * (2 * M + 6 * L + 1));
	cout << sizeof(unsigned long long) * (2 * M + 6 * L + 1);
	//PIs[i] = new byte[(2 * M + 6 * L + 1) * sizeof(ZZ_p)]();
	//set Message's size to: 2*M+6*L+2
	_msgs[(_id + 1) % NUM_OF_PARTIES]->setSize(F_VERIFY_ROUND1_MESSAGE, (2 * M + 6 * L + 1)*sizeof(ZZ_p));
	//*****need to check if this is ok..*****
	readFrom((_id + 1) % NUM_OF_PARTIES,PIs[(_id + 1) % NUM_OF_PARTIES]);
	_msgs[(_id + 2) % NUM_OF_PARTIES]->setSize(F_VERIFY_ROUND1_MESSAGE, (2 * M + 6 * L + 1) * sizeof(ZZ_p));
	readFrom((_id + 2) % NUM_OF_PARTIES, PIs[(_id + 2) % NUM_OF_PARTIES]);
	vector<vec_ZZ_p> parsedPIs;
	parsedPIs.resize(NUM_OF_PARTIES);
	for (int i = 0; i < NUM_OF_PARTIES; i++)
		if (i == _id)
			continue;
		else {
			parsedPIs[i].SetLength((2 * M + 6 * L + 1));
			for (int j = 0; j < (2 * M + 6 * L + 1); j++) {
				ZZ temp;
				NTL::ZZFromBytes(temp, &PIs[i][j * sizeof(ZZ_p)], sizeof(ZZ_p));
				parsedPIs[i][j] = PIs[i][j * sizeof(ZZ_p)];
				cout << "parsedPIs: " << parsedPIs[i] << endl;
			}
		}
	//(a)
	vector<ZZ_p> bettas;
	generateRandomElements(bettas, L);
	ZZ_p r;
	do {
		random(r);
	} while (rep(r) <= M);
	//(b)
	vector<ZZ_p> b;
	b.resize(NUM_OF_PARTIES);
	//store every polynomials result in f_r
	vec_ZZ_p f_r;
	f_r.SetLength(6 * L);
	for (int i = 0; i < NUM_OF_PARTIES; i++)
		for (int j = 0; j < M+1; j++) {
			if (i == _id)
				for (int l = 0; l < 6 * L; l++)//compute all of this party 6L polynomials at point r .e.g -f_l(r)
					f_r[l] += inputPolynomials[l][j] * NTL::power(r, j);
			else
				for (int k = 0; k < 2*M+1; k++) //computes b with every received p(x)
					b[i] += PIs[i][k + 6 * L] * NTL::power(r, k);
			b[i] *= bettas[j];//multiply each of the M g gate results with beta.
		}
	byte toSend[(6 * L + 2)*sizeof(ZZ_p)]{};
	for (int j = 0; j < 6 * L; j++)
		*(unsigned long long*)& toSend[j] = *(unsigned long long*)&f_r[j];
	sendTo((_id + 2) % NUM_OF_PARTIES, F_VERIFY_ROUND2_MESSAGE, toSend);
}
void Party::generateRandomElements(std::vector<ZZ_p>& thetas, int numOfElements)
{
	thetas.resize(numOfElements);
	//generate L random numbers 
	for (int i = 0; i < numOfElements; i++) {
		Share* randomShare = fRand();
		thetas.push_back(ZZ_p(finalReconstruct(*randomShare)));
		delete randomShare;
	}
}

void Party::interpolateInputPolynomials(unsigned int M, std::vector<vec_ZZ_p>& pointsToInterpolate, vec_ZZ_p& omegas, vector<ZZ_pX>& inputPolynomials)
{
	//build a sequance of points from 0 to M
	vec_ZZ_p range;
	range.SetLength(M + 1);
	for (int i = 0; i < M + 1; i++)
		range[i] = i;
	for (int i = 0; i < INPUTS_PER_MUL_GATE * L; i++) {
		//set number of coeffients of every polynomial to be M+1
		pointsToInterpolate[i].SetLength(M + 1);
		//put the witness coeffient as the free coeffient
		pointsToInterpolate[i][0] = omegas[i];
		for (int j = 0; j < M; j++)
			pointsToInterpolate[i][j+1] = this->_gGatesInputs[j * INPUTS_PER_MUL_GATE * L+i].getValue();//t'th input ,j'th coefficient of the polynomial
		//std::cout << "Range:" << range << " Len:" << range.length() << endl;
		std::cout << "pointsToInterpolate("<<i<<"):" << pointsToInterpolate[i] << " Len:" << pointsToInterpolate[i].length() << endl;
		interpolate(inputPolynomials[i], range, pointsToInterpolate[i]);
	}
}
void Party::verifyRound3(){
	byte buffers[NUM_OF_PARTIES][(6 * L + 2) * sizeof(ZZ_p)]{};
	for (int i = 0; i < NUM_OF_PARTIES; i++) {
		if (i == _id)
			continue;
	readFrom(i, buffers[i]);
	}
	for (int i = 0; i < F_VERIFY_ROUND2_MESSAGE_LEN; i++)
		*(unsigned long long*)& buffers[_id] = *(unsigned long long*) & buffers[(_id + 1) % NUM_OF_PARTIES][i] + *(unsigned long long*) & buffers[(_id + 2) % NUM_OF_PARTIES][i];
	if(*(unsigned long long*) & buffers[_id][F_VERIFY_ROUND2_MESSAGE_LEN-1])
		throw std::exception(__FUNCTION__  "ABORT!-I'm surrounded by liers!");
	cout << "Completed! No liers here" << endl;
}
