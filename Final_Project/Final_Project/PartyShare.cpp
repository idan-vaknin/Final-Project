#include "PartyShare.h"
PartyShare::PartyShare(Share* prevOutput , Party* p ){
	if (prevOutput)
		*this = *prevOutput;
	_owner = p;
}
void PartyShare::setParty(Party* p) {
	_owner = p; 
}
int PartyShare::correlatedRandomness() const {
	Share* randomNumbers = _owner->fRand();//memory needs to be released!!
	return (*randomNumbers)[_owner->getID()].getValue() - (*randomNumbers)[(_owner->getID() + 2) % NUM_OF_PARTIES].getValue();
}
Party* PartyShare::getParty()const { 
	return _owner; 
}
PartyShare::~PartyShare() {
	if (this->_owner)
		delete _owner;
}
Share& PartyShare::operator=(Share const& other) {
	*(Share*)this = other;
	return *this;
}