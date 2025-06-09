//============================================================================
// Name        : baseclient.cpp
// Author      : madMAx
// Version     :
// Copyright   : Your copyright notice
// Description : Hello World in C, Ansi-style
//============================================================================


#include "baseclient.h"
#include "sysinfo.h"
#include "prime.h"

#include <iostream>
#include <thread>

constexpr unsigned int poolMajorVersionRequired = 10;
constexpr unsigned int poolMinorVersionRequired = 3;

std::string gClientName;
unsigned gClientID;
unsigned gInstanceID;
const unsigned gClientVersion = 1061;

void* gCtx = 0;
static void* gFrontend = 0;
static void* gServer = 0;
static void* gSignals = 0;
static void* gWorkers = 0;

static unsigned gStaleShareChain = 0;

std::string gAddr = "";
proto::Block gBlock;
proto::ServerInfo gServerInfo;

static unsigned gNextReqID = 0;
static Timer gRequestTimer;
static unsigned gLatency = 0;
static bool gHeartBeat = true;
static bool gExit = false;
static bool gCompatible = false;

struct shareData {
  int length;
  decltype(std::chrono::steady_clock::now()) time;
};

static std::map<unsigned, shareData> gSharesSent;
static std::map<unsigned, share_t> gShares;
static BaseClient *gClient;


static bool ConnectBitcoin() {
	
	const proto::ServerInfo& sinfo = gServerInfo;
    LOG_F(INFO, "Connecting to bitcoin: %s:%d ...", sinfo.host().c_str(), sinfo.router());
	int linger = 0;
	char endpoint[256];
	snprintf(endpoint, sizeof(endpoint), "tcp://%s:%d", sinfo.host().c_str(), sinfo.router());	
	
	if(gServer) {
		zmq_close(gServer);
		gServer = nullptr;
	}
	
	gServer = zmq_socket(gCtx, ZMQ_DEALER);
	if(!gServer) {
        LOG_F(ERROR, "Failed to create bitcoin socket: %s", zmq_strerror(errno));
		return false;
	}
	
	if(zmq_setsockopt(gServer, ZMQ_LINGER, &linger, sizeof(int)) != 0) {
        LOG_F(ERROR, "Failed to set ZMQ_LINGER for bitcoin socket: %s", zmq_strerror(errno));
		zmq_close(gServer);
		gServer = nullptr;
		return false;
	}
	
	int err = zmq_connect(gServer, endpoint);
	if(err) {
        LOG_F(ERROR, "Can't connect to %s:%d (%s code: %d)", sinfo.host().c_str(), sinfo.router(), strerror(errno), errno);
		zmq_close(gServer);
		gServer = nullptr;
		return false;
	}
	
	return true;
}

static bool ConnectSignals() {
	
	const proto::ServerInfo& sinfo = gServerInfo;
    LOG_F(INFO, "Connecting to signals: %s:%d ...", sinfo.host().c_str(), sinfo.pub());
	int linger = 0;
	char endpoint[256];
	snprintf(endpoint, sizeof(endpoint), "tcp://%s:%d", sinfo.host().c_str(), sinfo.pub());
	
	if(gSignals) {
		zmq_close(gSignals);
		gSignals = nullptr;
	}
	
	gSignals = zmq_socket(gCtx, ZMQ_SUB);
	if(!gSignals) {
        LOG_F(ERROR, "Failed to create signals socket: %s", zmq_strerror(errno));
		return false;
	}
	
	if(zmq_setsockopt(gSignals, ZMQ_LINGER, &linger, sizeof(int)) != 0) {
        LOG_F(ERROR, "Failed to set ZMQ_LINGER for signals socket: %s", zmq_strerror(errno));
		zmq_close(gSignals);
		gSignals = nullptr;
		return false;
	}
	
	int err = zmq_connect(gSignals, endpoint);
	if(err) {
        LOG_F(ERROR, "Can't connect to %s:%d (%s code: %d)", sinfo.host().c_str(), sinfo.pub(), strerror(errno), errno);
		zmq_close(gSignals);
		gSignals = nullptr;
		return false;
	}
	
	const char one[2] = {1, 0};
	if(zmq_setsockopt(gSignals, ZMQ_SUBSCRIBE, one, 1) != 0) {
        LOG_F(ERROR, "Failed to set ZMQ_SUBSCRIBE for signals socket: %s", zmq_strerror(errno));
		zmq_close(gSignals);
		gSignals = nullptr;
		return false;
	}
	
	return true;
	
}


inline void GetNewReqNonce(proto::Request& req) {
	
	uint32_t limbs[8];
	for(int i = 0; i < 8; ++i)
		limbs[i] = rand();
	
	uint32_t tmp = limbs[0];
	for(int i = 1; i < 7; ++i)
		tmp *= limbs[i];
	
	limbs[7] = -tmp;
	req.set_reqnonce(&limbs[0], sizeof(limbs));
	
}


static void RequestWork() {
	
	if(!gServer)
		return;

	proto::Request req;
	req.set_type(proto::Request::GETWORK);
	req.set_reqid(++gNextReqID);
	req.set_version(gClientVersion);
	req.set_height(gBlock.height());
	
	GetNewReqNonce(req);
	Send(req, gServer);
	
	gRequestTimer.reset();
	
}


static void HandleNewBlock(const proto::Block& block, bool requestWork=true) {
	
	if(block.height() > gBlock.height()){
		gBlock = block;
		gClient->NotifyBlock(block);
    if (requestWork)
      RequestWork();
	}
	
}


static bool HandleNewWork(const proto::Work& work) {
  LOG_F(INFO, "Work received: height=%d diff=%.8g",
      work.height(), GetPrimeDifficulty(work.bits()));
	
	return gClient->TakeWork(work);
	
}


static void SubmitShare(const proto::Share& share) {
	
	proto::Request req;
	req.set_type(proto::Request::SHARE);
	req.set_reqid(++gNextReqID);
	req.set_version(gClientVersion);
	req.set_height(share.height());
	req.mutable_share()->CopyFrom(share);
	
	GetNewReqNonce(req);
	Send(req, gServer);
	
//  timeMap[req.reqid()] = time(0);
  shareData data;
  data.time = std::chrono::steady_clock::now();
  data.length = share.length();
  gSharesSent[req.reqid()] = data;
	
}


static void PrintStats() {
  char buffer[512] = "(ST/INV/DUP):";
	for(std::map<unsigned, share_t>::const_iterator iter = gShares.begin(); iter != gShares.end(); ++iter){
    char lbuffer[128];
		const share_t& share = iter->second;
    snprintf(lbuffer, sizeof(lbuffer), " %dx %dch(%d/%d/%d)", share.accepted, iter->first, share.stale, share.invalid, share.duplicate);
    strcat(buffer, lbuffer);
		
	}
	
	if(!gShares.size())
    strcat(buffer, " none");
    LOG_F(INFO, "%s", buffer);
}


static int HandleReply(void *socket) {
	
	proto::Reply rep;
	Receive(rep, socket);
	
  if(rep.has_errstr()) {
    LOG_F(ERROR, "Message from server: %s", rep.errstr().c_str());
    gExit = false;
    return -1;
  }
	
	if(rep.has_block()){
		
		HandleNewBlock(rep.block());
		
	}
	
	if(rep.type() == proto::Request::GETWORK){
		
		if(rep.has_work()){
			
			if (!HandleNewWork(rep.work())) {
				gExit = false;
				return -1;
			}
			
		}
		
	}else if(rep.type() == proto::Request::SHARE){
    auto currentTime = std::chrono::steady_clock::now();
    auto info = gSharesSent[rep.reqid()];
    auto ms = static_cast<unsigned>(std::chrono::duration_cast<std::chrono::milliseconds>(currentTime-info.time).count());
		gSharesSent.erase(rep.reqid());

		switch(rep.error()){
		case proto::Reply::NONE:
      LOG_F(1, "Share accepted %ums", ms);
			gStaleShareChain = 0;
      gShares[info.length].accepted++;
			break;
		case proto::Reply::INVALID:
      LOG_F(WARNING, "Invalid share %ums", ms);
      gShares[info.length].invalid++;
			break;
		case proto::Reply::STALE:
      LOG_F(WARNING, "Stale share %ums", ms);
      gShares[info.length].stale++;
			gStaleShareChain++;
			if (gStaleShareChain >= 4) {
				gStaleShareChain = 0;
				gExit = false;
				return -1;
			}
			break;
		case proto::Reply_ErrType_DUPLICATE:
      LOG_F(WARNING, "Duplicate share");
      gShares[info.length].duplicate++;
			break;
		default: break;
		}

    gLatency = ms;
	}else if(rep.type() == proto::Request::STATS){
		
    if(rep.error() == proto::Reply::NONE) {
    } else {
      LOG_F(WARNING, "Statistics rejected");
    }
		
	}else if(rep.type() == proto::Request::PING){
	}
	
	gHeartBeat = true;
	
	return 0;
	
}


static int HandleSignal(void *socket) {
	
	proto::Signal sig;
	ReceivePub(sig, socket);
	
	if(sig.type() == proto::Signal::NEWBLOCK){
    HandleNewBlock(sig.block(), false);
    if (sig.has_work()) {
      HandleNewWork(sig.work());
    } else {
      if (gCompatible) {
        LOG_F(ERROR, "Pool uses too old protocol version");
        RequestWork();
      } else {
        LOG_F(ERROR, "Pool uses too old protocol version, try run in compatible mode (xpmclient -c)");
        gExit = true;
        return -1;
      }
    }
		
	}else if(sig.type() == proto::Signal::SHUTDOWN){
		
    LOG_F(WARNING, "Server is shutting down, reconnecting...");
		gExit = false;
		return -1;
		
	}
	
	return 0;
	
}


static int HandleWorkers(void *socket) {
	
	proto::Share share;
	bool ret = Receive(share, socket);
	if(!ret)
		return 0;

	SubmitShare(share);
	
	return 0;
	
}


static int HandleTimer() {
	
	if(!gHeartBeat){
		
    LOG_F(WARNING, "Connection lost, reconnecting...");
		gExit = false;
		return -1;
		
	}
	
	{
		proto::Request req;
		req.set_type(proto::Request::STATS);
		req.set_reqid(++gNextReqID);
		req.set_version(gClientVersion);
		req.set_height(gBlock.height());
		
		proto::ClientStats& stats = *req.mutable_stats();
		int ngpus = gClient->GetStats(stats);
		stats.set_addr(gAddr);
		stats.set_name(gClientName);
		stats.set_clientid(gClientID);
		stats.set_instanceid(gInstanceID);
		stats.set_version(gClientVersion);
		stats.set_latency(gLatency);
		stats.set_ngpus(ngpus);
		stats.set_height(gBlock.height());
		
		GetNewReqNonce(req);
		Send(req, gServer);
	}
	
	PrintStats();
	
	gHeartBeat = false;
	
	//ReConnectSignals();
	
	return 0;
	
}


static int TimeoutCheckProc() {  
  auto currentTime = std::chrono::steady_clock::now();
  for (auto &t: gSharesSent) {
    if (std::chrono::duration_cast<std::chrono::seconds>(currentTime-t.second.time).count() >= 5) {
      gSharesSent.clear();
      LOG_F(WARNING, "Connection lost, reconnecting...");
      gExit = false;
      return -1;
    }
  }
  
  return 0;
  
}


int main(int argc, char **argv)
{
	char logFileName[64];
	{
		auto t = std::time(nullptr);
		auto now = std::localtime(&t);
		snprintf(logFileName, sizeof(logFileName), "miner-%04u-%02u-%02u.log", now->tm_year + 1900, now->tm_mon + 1, now->tm_mday);
	}
	loguru::g_stderr_verbosity = loguru::Verbosity_OFF;
	loguru::g_preamble_thread = false;
	loguru::g_preamble_file = false;
	loguru::g_flush_interval_ms = 100;
	loguru::init(argc, argv);
	loguru::add_file(logFileName, loguru::Append, loguru::Verbosity_INFO);
	loguru::g_stderr_verbosity = 1;

  	gBlock.set_height(0);
	gClientName = sysinfo::GetClientName();
	gClientID = sysinfo::GetClientID();
	gInstanceID = gClientID * (unsigned)time(0);
	srand(gInstanceID);
	
	std::string frontHost;
	unsigned frontPort;
	unsigned weaveDepth;
	Configuration* cfg = Configuration::create();
	try{
		cfg->parse("config.txt");
		frontHost = cfg->lookupString("", "server", "localhost");
		frontPort = cfg->lookupInt("", "port", 6666);
		gAddr = cfg->lookupString("", "address", "");
		gClientName = cfg->lookupString("", "name", gClientName.c_str());
    weaveDepth = cfg->lookupInt("", "weaveDepth", 40960);
	}catch(const ConfigurationException& ex){
    LOG_F(ERROR, "%s\n", ex.c_str());
		exit(EXIT_FAILURE);
	}
	
	if(!gClientName.size())
    gClientName = sysinfo::GetClientName();

	LOG_F(INFO, "xpmclient-%u.%u.%u", gClientVersion / 100, (gClientVersion % 100) / 10, (gClientVersion % 100) % 10);
	LOG_F(INFO, "ClientName = '%s'  ClientID = %u  InstanceID = %u", gClientName.c_str(), gClientID, gInstanceID);
	LOG_F(INFO, "Address = '%s'", gAddr.c_str());
	
	if(!gAddr.size()){
    LOG_F(ERROR, "address not specified in config.txt\n");
		exit(EXIT_FAILURE);
	}

	gCtx = zmq_ctx_new();
	gWorkers = zmq_socket(gCtx, ZMQ_PULL);
	zmq_bind(gWorkers, "inproc://shares");

  	gClient = createClient(gCtx);
  
  	bool benchmarkOnly = false;
  	if (argc >= 2) {
    if (strcmp(argv[1], "-b") == 0 || strcmp(argv[1], "--benchmark") == 0)
      benchmarkOnly = true;
    else if (strcmp(argv[1], "-c") == 0)
      gCompatible = true;
  }
	gExit = !gClient->Initialize(cfg, benchmarkOnly);
	
  	while(!gExit){
		gBlock.Clear();
		proto::Reply rep;

    bool frontendConnected = false;
    LOG_F(INFO, "Connecting to frontend: %s:%d ...", frontHost.c_str(), frontPort);

    while (!frontendConnected) {
      int result;
			int linger = 0;
			char endpoint[256];
			snprintf(endpoint, sizeof(endpoint), "tcp://%s:%d", frontHost.c_str(), frontPort);
			
			if(gFrontend) {
				zmq_disconnect(gFrontend, endpoint);
				if(zmq_setsockopt(gFrontend, ZMQ_LINGER, &linger, sizeof(int)) != 0) {
		LOG_F(ERROR, "Failed to set ZMQ_LINGER for frontend socket: %s", zmq_strerror(errno));
				}
				zmq_close(gFrontend);
				gFrontend = nullptr;
			}
			
			gFrontend = zmq_socket(gCtx, ZMQ_DEALER);
			if(!gFrontend) {
        LOG_F(ERROR, "Failed to create frontend socket: %s", zmq_strerror(errno));
				continue;
			}
			
			if(zmq_setsockopt(gFrontend, ZMQ_LINGER, &linger, sizeof(int)) != 0) {
        LOG_F(ERROR, "Failed to set ZMQ_LINGER for frontend socket: %s", zmq_strerror(errno));
				zmq_close(gFrontend);
				gFrontend = nullptr;
				continue;
			}
			
      if ( (result = zmq_connect(gFrontend, endpoint)) == 0 ) {
        proto::Request req;
        req.set_type(proto::Request::CONNECT);
        req.set_reqid(++gNextReqID);
        req.set_version(gClientVersion);
        req.set_height(0);
        GetNewReqNonce(req);
        Send(req, gFrontend);

        bool ready = czmq_poll(gFrontend, 3000);
        if (ready) {
          Receive(rep, gFrontend);
          if (rep.error() == proto::Reply::NONE) {
            if (rep.has_sinfo()) {
              gServerInfo = rep.sinfo();
              bool versionCompatible =
                gServerInfo.has_versionmajor() &&
                gServerInfo.has_versionminor() &&
                  (gServerInfo.versionmajor() > poolMajorVersionRequired ||
                  (gServerInfo.versionmajor() == poolMajorVersionRequired && gServerInfo.versionminor() >= poolMinorVersionRequired));
              if (versionCompatible || gCompatible) {
                if (!versionCompatible)
                  LOG_F(ERROR, "Pool uses too old protocol version (%u.%u or higher required)", poolMajorVersionRequired, poolMinorVersionRequired);
                if (ConnectBitcoin() && ConnectSignals()) {
                  // set config
                  proto::Request req;
                  req.set_type(proto::Request::SETCONFIG);
                  req.set_reqid(++gNextReqID);
                  req.set_weavedepth(weaveDepth);
                  Send(req, gServer);

                  frontendConnected = true;
                }
              } else {
                LOG_F(ERROR, "Pool uses too old protocol version (%u.%u or higher required), try run in compatible mode (xpmclient -c)", poolMajorVersionRequired, poolMinorVersionRequired);
                exit(EXIT_FAILURE);
              }
            } else {
              gExit = true;
              LOG_F(ERROR, "Pool protocol mismatch (!has_sinfo)");
            }
          } else {
            LOG_F(ERROR, "%s", proto::Reply::ErrType_Name(rep.error()).c_str());
            if(rep.has_errstr())
              LOG_F(ERROR, "Message from server: %s", rep.errstr().c_str());
          }
        }

        if (!frontendConnected) {
          zmq_disconnect(gFrontend, endpoint);
          zmq_close(gFrontend);
          gFrontend = nullptr;
        }
      } else {
        LOG_F(ERROR, "Can't connect to %s:%d (%s code: %d)", frontHost.c_str(), frontPort, strerror(errno), errno);
        zmq_close(gFrontend);
        gFrontend = nullptr;
      }

      if (!frontendConnected) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
      }
    }
		
		bool loopActive = true;
		time_t timer1sec = time(nullptr);
		time_t timer1min = time(nullptr);
		zmq_pollitem_t items[] = {
			{gServer, 0, ZMQ_POLLIN, 0},
			{gSignals, 0, ZMQ_POLLIN, 0},
			{gWorkers, 0, ZMQ_POLLIN, 0}
		};
			
			gHeartBeat = true;
			gExit = true;
			
			if(rep.has_block())
				HandleNewBlock(rep.block());
			else
				RequestWork();


      gClient->Toggle();
			while (loopActive) {
				int result = zmq_poll(items, sizeof(items)/sizeof(zmq_pollitem_t), 1000);
				if (result == -1)
					break;
			 
				if (result > 0) {
					if (items[0].revents & ZMQ_POLLIN)
						loopActive &= (HandleReply(gServer) == 0);
					if (items[1].revents & ZMQ_POLLIN)
						loopActive &= (HandleSignal(gSignals) == 0);
					if (items[2].revents & ZMQ_POLLIN)
						loopActive &= (HandleWorkers(gWorkers) == 0);
				}
				
				// check timers
				time_t currentTime = time(0);
				if (currentTime - timer1sec >= 1) {
					timer1sec = currentTime;
					loopActive &= (TimeoutCheckProc() == 0);
				}
				
				if (currentTime - timer1min >= 60) {
					timer1min = currentTime;
					loopActive &= (HandleTimer() == 0);
				}
			}                

      gClient->Toggle();
		zmq_close(gServer);
		zmq_close(gSignals);
		gServer = 0;
		gSignals = 0;
    std::this_thread::sleep_for(std::chrono::seconds(5));
	}
	
	delete gClient;
	
	if(gWorkers) {
		zmq_close(gWorkers);
		gWorkers = nullptr;
	}
	
	if(gFrontend) {
		zmq_close(gFrontend); 
		gFrontend = nullptr;
	}

	if(gServer) {
		zmq_close(gServer);
		gServer = nullptr;
	}

	if(gSignals) {
		zmq_close(gSignals);
		gSignals = nullptr;
	}

	if(gCtx) {
		zmq_ctx_shutdown(gCtx);
		zmq_ctx_term(gCtx);
		gCtx = nullptr;
	}
	
	return EXIT_SUCCESS;
	
}

BaseClient::BaseClient(void *ctx)
{
  mCtx = ctx;
  mBlockPub = zmq_socket(ctx, ZMQ_PUB);
  mWorkPub = zmq_socket(ctx, ZMQ_PUB);
  mStatsPull = zmq_socket(ctx, ZMQ_PULL);
 
  zmq_bind(mBlockPub, "inproc://blocks");
  zmq_bind(mWorkPub, "inproc://work");
  zmq_bind(mStatsPull, "inproc://stats");

  mPaused = true;
  mNumDevices = 0;
  mStatCounter = 0;
}
