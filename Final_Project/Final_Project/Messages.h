#pragma once
#define WINDOWS_IGNORE_PACKING_MISMATCH
#include<cstdint>

#define KEY_LEN 256
#define SEQ_LEN 4
#define RECONSTRUCT_LEN 64
#define MAX_MESSAGE_SIZE 256
#define NUM_OF_PARTIES 3
#define AES_SIZE 16

#define BASE_PORT 62000
#define BASE_IP "192.168.0."

#define HEADER_SIZE 3

enum types{SEQ = 1,KEY,RECONSTRUCT};

#pragma pack(1)//allow no padding
typedef class Message {
private:
	uint8_t _type;
	unsigned short _size;
	char* _data; 
	bool _isRead = true;
public:
	Message(unsigned char type = 0):_type(type),_size(0){
		setSize(type);
		_data = new char[1+(type?_size :MAX_MESSAGE_SIZE)]();//Increment by 1 for null character. Allocate MAX_MESSAGE_SIZE in case type = 0.
	}
	void setSize(int type) {
		switch (type)
		{
		case SEQ:
			_size = SEQ_LEN;
			break;
		case KEY:
			_size = KEY_LEN;
			break;
		case RECONSTRUCT:
			_size = RECONSTRUCT_LEN;
			break;
		default:
			break;
		}
	}
	short getSize()const { return _size; }
	bool getIsRead()const { return _isRead; }
	void setData(const char* dataPtr) {
		memcpy(_data , dataPtr, _size);
	}
	void setIsRead(bool val) { this->_isRead = val; }
	char* getData()const { return _data; }

	~Message(){
		if(_data)
			delete[] _data;
		_data = nullptr;
	}
} Message;