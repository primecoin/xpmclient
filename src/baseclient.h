/*
 * baseclient.h
 *
 *  Created on: 30.04.2014
 *      Author: mad
 */

#ifndef BASECLIENT_H_
#define BASECLIENT_H_


#include <stdio.h>
#include <stdlib.h>
#include <string>

#include "protocol.pb.h"
using namespace pool;

#include <zmq.h>
#include <chrono>

#include <locale.h>
#include <config4cpp/Configuration.h>
#include "zmqextras.h"
#include "loguru.hpp"
using namespace config4cpp;

class BaseClient;

double GetPrimeDifficulty(unsigned int nBits);
BaseClient *createClient(void *ctx);

extern std::string gClientName;
extern unsigned gClientID;
extern const unsigned gClientVersion;

extern std::string gAddr;
extern proto::Block gBlock;

class Timer {
public:
    Timer() {
        reset();
    }
    void reset() {
        m_timestamp = std::chrono::high_resolution_clock::now();
    }
    float diff() {
        std::chrono::duration<float> fs = std::chrono::high_resolution_clock::now() - m_timestamp;
        return fs.count();
    }
private:
    std::chrono::high_resolution_clock::time_point m_timestamp;
};


struct share_t {
	
	share_t(){
		accepted = 0;
		invalid = 0;
		stale = 0;
		duplicate = 0;
	}
	
	unsigned accepted;
	unsigned invalid;
	unsigned stale;
	unsigned duplicate;
	
};


template<class C>
static bool Receive(C& rep, void* socket) {
  bool result = false;
  zmq_msg_t msg;
  zmq_msg_init(&msg);
  if (zmq_recvmsg(socket, &msg, 0) != -1) {
    if (rep.ParseFromArray(zmq_msg_data(&msg), zmq_msg_size(&msg))) {
      result = true;
    } else {
      LOG_F(ERROR, "Invalid %u bytes data detected (Receive)", static_cast<unsigned>(zmq_msg_size(&msg)));
    }
  } else {
    LOG_F(ERROR, "Receive message failed (Receive)");
  }

  zmq_msg_close(&msg);  
  return result;
}

template<class C>
static bool ReceivePub(C& sig, void* socket) {
  bool result = false;
  zmq_msg_t msg;
  zmq_msg_init (&msg);
  if (zmq_recvmsg (socket, &msg, 0) != -1) {
    size_t size = zmq_msg_size(&msg);
    if (size && sig.ParseFromArray(((char*)zmq_msg_data(&msg))+1, zmq_msg_size(&msg)-1)) {
      result = true;
    } else {
      LOG_F(ERROR, "Invalid data detected (ReceivePub)");
    }
  } else {
    LOG_F(ERROR, "Receive message failed (ReceivePub)");
  }

  zmq_msg_close (&msg);
  return result;
}


template<class C>
static void Send(const C& req, void* socket) {
  uint8_t buffer[4096];
  size_t fsize = req.ByteSize();
  void *data = (fsize <= sizeof(buffer)) ? buffer : malloc(fsize);
  req.SerializeToArray(data, fsize);
  if (zmq_send(socket, data, fsize, 0) == -1)
    LOG_F(ERROR, "Send message failed (size=%u)", (unsigned)fsize);
  if (data != buffer)
    free(data);
}


template<class C>
static void SendPub(const C& sig, void* socket) {
  uint8_t buffer[4096];
  size_t fsize = sig.ByteSize();
  unsigned char *data = ( (fsize+1) <= sizeof(buffer)) ? buffer : static_cast<unsigned char*>(malloc(fsize+1));
  data[0] = 1;
  sig.SerializeToArray(data+1, fsize);
  if (zmq_send(socket, data, fsize+1, 0) == -1)
    LOG_F(ERROR, "SendPub message failed (size=%u)", (unsigned)fsize);
  if (data != buffer)
    free(data);
}



class BaseClient {
public:
  
  BaseClient(void *ctx);
  virtual ~BaseClient() {}
  
  virtual bool Initialize(Configuration* cfg, bool benchmarkOnly, unsigned adjustedKernelTarget = 0) = 0;
  virtual void NotifyBlock(const proto::Block& block) = 0;
  virtual bool TakeWork(const proto::Work& work) = 0;
  virtual int GetStats(proto::ClientStats& stats) = 0;
  virtual void Toggle() = 0;
  
protected:
  void* mCtx;
  
  std::map<unsigned,int> mDeviceMap;
  std::map<unsigned,int> mDeviceMapRev;
  
  void* mBlockPub;
  void* mWorkPub;
  void* mStatsPull;
  
  unsigned mNumDevices;
  unsigned mStatCounter;
  bool mPaused;
  
  std::vector<int> mCoreFreq;
  std::vector<int> mMemFreq;
  std::vector<int> mPowertune;
  std::vector<int> mFanSpeed;
   
  const char *onCrash;
};


#endif /* BASECLIENT_H_ */
