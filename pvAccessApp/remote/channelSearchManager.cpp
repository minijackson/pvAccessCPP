/*
 * channelSearchManager.cpp
 */

#include <pv/channelSearchManager.h>

using namespace std;
using namespace epics::pvData;

namespace epics { namespace pvAccess {

const int BaseSearchInstance::DATA_COUNT_POSITION = CA_MESSAGE_HEADER_SIZE + sizeof(int32)/sizeof(int8) + 1;
const int BaseSearchInstance::PAYLOAD_POSITION = sizeof(int16)/sizeof(int8) + 2;

void BaseSearchInstance::initializeSearchInstance()
{
    _owner = NULL;
    _ownerMutex = NULL;
    _ownerIndex = -1;
 }

void BaseSearchInstance::unsetListOwnership()
{
	Lock guard(_mutex);
	//if (_owner != NULL) this->release();
	_owner = NULL;
}

void BaseSearchInstance::addAndSetListOwnership(SearchInstance::List* newOwner, Mutex* ownerMutex, int32 index)
{
	if(ownerMutex == NULL) THROW_BASE_EXCEPTION("Null owner mutex");

	_ownerMutex = ownerMutex;
	Lock ownerGuard(*_ownerMutex);
	Lock guard(_mutex);
	newOwner->push_back(this);
	//if (_owner == NULL) this->acquire(); // new owner
	_owner = newOwner;
	_ownerIndex = index;
}

void BaseSearchInstance::removeAndUnsetListOwnership()
{
	if(_owner == NULL) return;

	if(_ownerMutex == NULL) THROW_BASE_EXCEPTION("Null owner mutex");
	Lock ownerGuard(*_ownerMutex);
	Lock guard(_mutex);
	if(_owner != NULL)
	{
	    //this->release();
        // TODO !!!
        for (SearchInstance::List::iterator iter = _owner->begin(); iter != _owner->end(); iter++)
            if (*iter == this)
            {
                _owner->erase(iter);
                break;
            }
		_owner = NULL;
	}
}

int32 BaseSearchInstance::getOwnerIndex()
{
	Lock guard(_mutex);
	int32 retval = _ownerIndex;
	return retval;
}

bool BaseSearchInstance::generateSearchRequestMessage(ByteBuffer* requestMessage, TransportSendControl* control)
{
	int16 dataCount = requestMessage->getShort(DATA_COUNT_POSITION);

	dataCount++;
    /*
	if(dataCount >= MAX_SEARCH_BATCH_COUNT)
	{
		return false;
	}
    */

	const String name = getSearchInstanceName();
	// not nice...
	const int addedPayloadSize = sizeof(int32)/sizeof(int8) + (1 + sizeof(int32)/sizeof(int8) + name.length());

	if(((int)requestMessage->getRemaining()) < addedPayloadSize)
	{
		return false;
	}

	requestMessage->putInt(getSearchInstanceID());
	SerializeHelper::serializeString(name, requestMessage, control);

	requestMessage->putInt(PAYLOAD_POSITION, requestMessage->getPosition() - CA_MESSAGE_HEADER_SIZE);
	requestMessage->putShort(DATA_COUNT_POSITION, dataCount);
	return true;
}

const int32 SearchTimer::MAX_FRAMES_PER_TRY = 64;

SearchTimer::SearchTimer(ChannelSearchManager* _chanSearchManager, int32 timerIndex, bool allowBoost, bool allowSlowdown):
		_chanSearchManager(_chanSearchManager),
		_searchAttempts(0),
		_searchRespones(0),
		_framesPerTry(1),
		_framesPerTryCongestThresh(DBL_MAX),
		_startSequenceNumber(0),
		_endSequenceNumber(0),
		_timerIndex(timerIndex),
		_allowBoost(allowBoost),
		_allowSlowdown(allowSlowdown),
        _requestPendingChannels(new SearchInstance::List()),
		_responsePendingChannels(new SearchInstance::List()),
		_timerNode(new TimerNode(*this)),
		_canceled(false),
		_timeAtResponseCheck(0)
{

}

SearchTimer::~SearchTimer()
{
	if(_requestPendingChannels) delete _requestPendingChannels;
	if(_responsePendingChannels) delete _responsePendingChannels;
	if(_timerNode) delete _timerNode;
}

void SearchTimer::shutdown()
{
	Lock guard(_mutex); //the whole method is locked

	{
		Lock guard(_volMutex);
		if(_canceled) return;
		_canceled = true;
	}

	{
		Lock guard(_requestPendingChannelsMutex);
		_timerNode->cancel();

		_requestPendingChannels->clear();
		_responsePendingChannels->clear();
	}
}

void SearchTimer::installChannel(SearchInstance* channel)
{
	Lock guard(_mutex); //the whole method is locked
	if(_canceled) return;

	Lock pendingChannelGuard(_requestPendingChannelsMutex);
	bool startImmediately = _requestPendingChannels->empty();
	channel->addAndSetListOwnership(_requestPendingChannels, &_requestPendingChannelsMutex, _timerIndex);

	// start searching
	if(startImmediately)
	{
		_timerNode->cancel();
		if(_timeAtResponseCheck == 0)
		{
			TimeStamp current;
			current.getCurrent();
			_timeAtResponseCheck = current.getMilliseconds();
		}

		// start with some initial delay (to collect all installed requests)
		_chanSearchManager->_context->getTimer()->scheduleAfterDelay(*_timerNode, 0.01);
	}
}

void SearchTimer::moveChannels(SearchTimer* destination)
{
	// do not sync this, not necessary and might cause deadlock
	while(!_responsePendingChannels->empty())
	{
        SearchInstance* channel = _responsePendingChannels->front();
        _responsePendingChannels->pop_front();
        
		{
			Lock guard(_volMutex);
			if(_searchAttempts > 0)
			{
				_searchAttempts--;
			}
		}
		destination->installChannel(channel);
	}

	// bulk move
	Lock guard(_requestPendingChannelsMutex);
	while (!_requestPendingChannels->empty())
	{
        SearchInstance* channel = _requestPendingChannels->front();
        _requestPendingChannels->pop_front();

		destination->installChannel(channel);
	}
}

void SearchTimer::timerStopped()
{
	//noop
}

void SearchTimer::callback()
{
	{
		Lock guard(_volMutex);
		if(_canceled) return;
	}

	// if there was some success (no congestion)
	// boost search period (if necessary) for channels not recently searched
	int32 searchRespones;
	{
		Lock guard(_volMutex);
		searchRespones = _searchRespones;
	}
	if(_allowBoost && searchRespones > 0)
	{
		Lock guard(_requestPendingChannelsMutex);
		while(!_requestPendingChannels->empty())
		{
			SearchInstance* channel = _requestPendingChannels->front();
			// boost needed check
			//final int boostIndex = searchRespones >= searchAttempts * SUCCESS_RATE ? Math.min(Math.max(0, timerIndex - 1), beaconAnomalyTimerIndex) : beaconAnomalyTimerIndex;
			const int boostIndex = _chanSearchManager->_beaconAnomalyTimerIndex;
			if(channel->getOwnerIndex() > boostIndex)
			{
				_requestPendingChannels->pop_front();
				//channel->acquire();
				channel->unsetListOwnership();
				_chanSearchManager->boostSearching(channel, boostIndex);
				//channel->release();
			}
		}
	}

	SearchInstance* channel;

	// should we check results (installChannel trigger timer immediately)
	TimeStamp current;
	current.getCurrent();
	int64 now = current.getMilliseconds();
	if(now - _timeAtResponseCheck >= period())
	{
		_timeAtResponseCheck = now;

		// notify about timeout (move it to other timer)
		while(!_responsePendingChannels->empty())
		{
            channel = _responsePendingChannels->front();
            _responsePendingChannels->pop_front();
            
			if(_allowSlowdown)
			{
			    //channel->acquire();
				channel->unsetListOwnership();
				_chanSearchManager->searchResponseTimeout(channel, _timerIndex);
				//channel->release();
			}
			else
			{
				channel->addAndSetListOwnership(_requestPendingChannels, &_requestPendingChannelsMutex, _timerIndex);
			}
		}

		int32 searchRespones,searchAttempts;
		{
			Lock guard(_volMutex);
			searchAttempts = _searchAttempts;
			searchRespones = _searchRespones;
		}
		// check search results
		if(searchAttempts > 0)
		{
            // increase UDP frames per try if we have a good score
			if(searchRespones >= searchAttempts * ChannelSearchManager::SUCCESS_RATE)
			{
				// increase frames per try
                // a congestion avoidance threshold similar to TCP is now used
				if(_framesPerTry < MAX_FRAMES_PER_TRY)
				{
					if(_framesPerTry < _framesPerTryCongestThresh)
					{
						_framesPerTry = min(2*_framesPerTry, _framesPerTryCongestThresh);
					}
					else
					{
						_framesPerTry += 1.0/_framesPerTry;
					}
				}
			}
			else
			{
				// decrease frames per try, fallback
				_framesPerTryCongestThresh = _framesPerTry / 2.0;
				_framesPerTry = 1;
			}

		}
	}


	{
		Lock guard(_volMutex);
		_startSequenceNumber = _chanSearchManager->getSequenceNumber();
		_searchAttempts = 0;
		_searchRespones = 0;
	}

	int32 framesSent = 0;
	int32 triesInFrame = 0;

	// reschedule
	bool canceled;
	{
		Lock guard(_volMutex);
		canceled = _canceled;
	}


	{
		Lock guard(_requestPendingChannelsMutex);
        if (_requestPendingChannels->empty())
            channel = 0;
        else {
            channel = _requestPendingChannels->front();
            _requestPendingChannels->pop_front();
        }
            
	}
	while (!canceled && channel != NULL)
	{
	    //channel->acquire();
		channel->unsetListOwnership();

		bool requestSent = true;
		bool allowNewFrame = (framesSent+1) < _framesPerTry;

                {
                        Lock guard(_volMutex);
                        _endSequenceNumber = _chanSearchManager->getSequenceNumber() + 1;
                }

		bool frameWasSent = _chanSearchManager->generateSearchRequestMessage(channel, allowNewFrame);
		if(frameWasSent)
		{
			framesSent++;
			triesInFrame = 0;
			if(!allowNewFrame)
			{
				channel->addAndSetListOwnership(_requestPendingChannels, &_requestPendingChannelsMutex, _timerIndex);
				requestSent = false;
			}
			else
			{
				triesInFrame++;
			}
		}
		else
		{
			triesInFrame++;
		}

		if(requestSent)
		{
			channel->addAndSetListOwnership(_responsePendingChannels, &_responsePendingChannelsMutex, _timerIndex);
			Lock guard(_volMutex);
			if(_searchAttempts < INT_MAX)
			{
				_searchAttempts++;
			}
		}
		
		//channel->release();

		// limit
		if(triesInFrame == 0 && !allowNewFrame) break;

		{
			Lock guard(_volMutex);
			canceled = _canceled;
		}

		{
			Lock guard(_requestPendingChannelsMutex);
            if (_requestPendingChannels->empty())
                channel = 0;
            else {
                channel = _requestPendingChannels->front();
                _requestPendingChannels->pop_front();
            }
		}
	}


    // flush out the search request buffer
	if(triesInFrame > 0)
	{
	   
        {
                	Lock guard(_volMutex);
                	_endSequenceNumber = _chanSearchManager->getSequenceNumber() + 1;
		}
		
		_chanSearchManager->flushSendBuffer();
		framesSent++;
	}


	{
		Lock guard(_volMutex);
		_endSequenceNumber = _chanSearchManager->getSequenceNumber();
	    //printf("[%d] sn %d -> %d\n", _timerIndex, _startSequenceNumber, _endSequenceNumber);

		// reschedule
		canceled = _canceled;
	}
	Lock guard(_requestPendingChannelsMutex);
	if(!canceled && !_timerNode->isScheduled())
	{
		bool someWorkToDo = (!_requestPendingChannels->empty() || !_responsePendingChannels->empty());
		if(someWorkToDo)
		{
			_chanSearchManager->_context->getTimer()->scheduleAfterDelay(*_timerNode, period()/1000.0);
		}
	}
}

void SearchTimer::searchResponse(int32 responseSequenceNumber, bool isSequenceNumberValid, int64 responseTime)
{
	bool validResponse = true;
	{
		Lock guard(_volMutex);
		if(_canceled) return;

		if(isSequenceNumberValid)
		{
			validResponse = _startSequenceNumber <= responseSequenceNumber && responseSequenceNumber <= _endSequenceNumber;
		}
        //if (!validResponse)
	    //   printf("[%d] not valid response %d < %d < %d\n", _timerIndex, _startSequenceNumber,responseSequenceNumber, _endSequenceNumber);
	}


	// update RTTE
	if(validResponse)
	{
		const int64 dt = responseTime - _chanSearchManager->getTimeAtLastSend();
		_chanSearchManager->updateRTTE(dt);
		Lock guard(_volMutex);
		if(_searchRespones < INT_MAX)
		{
			_searchRespones++;

			// all found, send new search requests immediately if necessary
			if(_searchRespones == _searchAttempts)
			{
				if(_requestPendingChannels->size() > 0)
				{
					_timerNode->cancel();
					_chanSearchManager->_context->getTimer()->scheduleAfterDelay(*_timerNode, 0.0);
				}
			}
		}
	}
}

const int64 SearchTimer::period()
{
	return (int64) ((1 << _timerIndex) * _chanSearchManager->getRTTE());
}

const int64 ChannelSearchManager::MIN_RTT = 32;
const int64 ChannelSearchManager::MAX_RTT = 2 * ChannelSearchManager::MIN_RTT;
const double ChannelSearchManager::SUCCESS_RATE = 0.9;
const int64 ChannelSearchManager::MAX_SEARCH_PERIOD = 5 * 60000;
const int64 ChannelSearchManager::MAX_SEARCH_PERIOD_LOWER_LIMIT = 60000;
const int64 ChannelSearchManager::BEACON_ANOMALY_SEARCH_PERIOD = 5000;
const int32 ChannelSearchManager::MAX_TIMERS = 18;

ChannelSearchManager::ChannelSearchManager(Context* context):
						_context(context),
						_canceled(false),
						_rttmean(MIN_RTT),
						_sequenceNumber(0)
{
	// create and initialize send buffer
	_sendBuffer = new ByteBuffer(MAX_UDP_SEND);
	initializeSendBuffer();

	// TODO should be configurable
	int64 maxPeriod = MAX_SEARCH_PERIOD;

	maxPeriod = min(maxPeriod, MAX_SEARCH_PERIOD_LOWER_LIMIT);

	// calculate number of timers to reach maxPeriod (each timer period is doubled)
	double powerOfTwo = log(maxPeriod / (double)MIN_RTT) / log(2.0);
	int32 numberOfTimers = (int32)(powerOfTwo + 1);
	numberOfTimers = min(numberOfTimers, MAX_TIMERS);

	// calculate beacon anomaly timer index
	powerOfTwo = log(BEACON_ANOMALY_SEARCH_PERIOD  / (double)MIN_RTT) / log(2.0);
	_beaconAnomalyTimerIndex = (int32)(powerOfTwo + 1);
	_beaconAnomalyTimerIndex = min(_beaconAnomalyTimerIndex, numberOfTimers - 1);

	// create timers
	_timers = new SearchTimer*[numberOfTimers];
	for(int32 i = 0; i < numberOfTimers; i++)
	{
		_timers[i] = new SearchTimer(this, i, i > _beaconAnomalyTimerIndex, i != (numberOfTimers-1));
	}
	_numberOfTimers = numberOfTimers;

	_mockTransportSendControl = new MockTransportSendControl();
}

ChannelSearchManager::~ChannelSearchManager()
{
	for(int32 i = 0; i < _numberOfTimers; i++)
	{
		if(_timers[i]) delete _timers[i];
	}
	if(_timers) delete[] _timers;
	if(_sendBuffer) delete _sendBuffer;
	if(_mockTransportSendControl) delete _mockTransportSendControl;
}

void ChannelSearchManager::cancel()
{
	Lock guard(_mutex);

	{
		Lock guard(_volMutex);
		if(_canceled) return;

		_canceled = true;
	}

	if(_timers != NULL)
	{
		for(int i = 0; i < _numberOfTimers; i++)
		{
			_timers[i]->shutdown();
		}
	}
}

int32 ChannelSearchManager::registeredChannelCount()
{
	Lock guard(_channelMutex);
	return _channels.size();
}

void ChannelSearchManager::registerChannel(SearchInstance* channel)
{
	{
		Lock guard(_volMutex);
		if(_canceled) return;
	}

	Lock guard(_channelMutex);
	//overrides if already registered
	_channels[channel->getSearchInstanceID()] = channel;
	_timers[0]->installChannel(channel);
}

void ChannelSearchManager::unregisterChannel(SearchInstance* channel)
{
	Lock guard(_channelMutex);
	_channelsIter = _channels.find(channel->getSearchInstanceID());
	if(_channelsIter != _channels.end())
	{
		_channels.erase(channel->getSearchInstanceID());
	}

	channel->removeAndUnsetListOwnership();
}

void ChannelSearchManager::searchResponse(int32 cid, int32 seqNo, int8 minorRevision, osiSockAddr* serverAddress)
{
	Lock guard(_channelMutex);
	
	// first remove
	SearchInstance* si = NULL;
	_channelsIter = _channels.find(cid);
	if(_channelsIter != _channels.end())
	{
		si = _channelsIter->second;
		_channels.erase(_channelsIter);
		//si->acquire();
		si->removeAndUnsetListOwnership();

    	// report success
    	const int timerIndex = si->getOwnerIndex();
    	TimeStamp now;
    	now.getCurrent();
    	_timers[timerIndex]->searchResponse(seqNo, seqNo != 0, now.getMilliseconds());
    
    	guard.unlock();

    	// then noftify SearchInstance
    	si->searchResponse(minorRevision, serverAddress);
    	//si->release();
	}
	else
	{
    	guard.unlock();

		// minor hack to enable duplicate reports
		si = dynamic_cast<SearchInstance*>(_context->getChannel(cid).get()); // TODO
		if(si != NULL)
		{
		    //si->acquire();    // TODO not thread/destruction safe
			si->searchResponse(minorRevision, serverAddress);
			//si->release();
		}
		return;
	}
}

void ChannelSearchManager::beaconAnomalyNotify()
{
	for(int i = _beaconAnomalyTimerIndex + 1; i < _numberOfTimers; i++)
	{
		_timers[i]->moveChannels(_timers[_beaconAnomalyTimerIndex]);
	}
}

void ChannelSearchManager::initializeSendBuffer()
{
	Lock guard(_volMutex);
	_sequenceNumber++;


	// new buffer
	_sendBuffer->clear();
    _sendBuffer->putByte(CA_MAGIC);
    _sendBuffer->putByte(CA_VERSION);
    _sendBuffer->putByte((EPICS_BYTE_ORDER == EPICS_ENDIAN_BIG) ? 0x80 : 0x00); // data + 7-bit endianess
	_sendBuffer->putByte((int8)3);	// search
	_sendBuffer->putInt(sizeof(int32)/sizeof(int8) + 1);		// "zero" payload
	_sendBuffer->putInt(_sequenceNumber);

	/*
	final boolean REQUIRE_REPLY = false;
	sendBuffer.put(REQUIRE_REPLY ? (byte)QoS.REPLY_REQUIRED.getMaskValue() : (byte)QoS.DEFAULT.getMaskValue());
	*/

	_sendBuffer->putByte((int8)QOS_DEFAULT);
	_sendBuffer->putShort((int16)0);	// count
}

void ChannelSearchManager::flushSendBuffer()
{
	Lock guard(_mutex);
	Lock volGuard(_volMutex);
	TimeStamp now;
	now.getCurrent();
	_timeAtLastSend = now.getMilliseconds();
    
    Transport::shared_pointer tt = _context->getSearchTransport();
    BlockingUDPTransport::shared_pointer ut = std::tr1::static_pointer_cast<BlockingUDPTransport>(tt); 
	ut->send(_sendBuffer); // TODO
	initializeSendBuffer();
}

bool ChannelSearchManager::generateSearchRequestMessage(SearchInstance* channel, bool allowNewFrame)
{
	Lock guard(_mutex);
	bool success = channel->generateSearchRequestMessage(_sendBuffer, _mockTransportSendControl);
	// buffer full, flush
	if(!success)
	{
		flushSendBuffer();
		if(allowNewFrame)
		{
			channel->generateSearchRequestMessage(_sendBuffer, _mockTransportSendControl);
		}
		return true;
	}
	return false;
}

void ChannelSearchManager::searchResponseTimeout(SearchInstance* channel, int32 timerIndex)
{
	int32 newTimerIndex = min(++timerIndex, _numberOfTimers - 1);
	_timers[newTimerIndex]->installChannel(channel);
}

void ChannelSearchManager::boostSearching(SearchInstance* channel, int32 timerIndex)
{
	_timers[timerIndex]->installChannel(channel);
}

inline void ChannelSearchManager::updateRTTE(long rtt)
{
	Lock guard(_volMutex);
	const double error = rtt - _rttmean;
	_rttmean += error / 4.0;
}

inline double ChannelSearchManager::getRTTE()
{
	Lock guard(_volMutex);
	double rtte =  min(max((double)_rttmean, (double)MIN_RTT), (double)MAX_RTT);
	return rtte;
}

inline int32 ChannelSearchManager::getSequenceNumber()
{
	Lock guard(_volMutex);
	int32 retval = _sequenceNumber;
	return retval;
}

inline int64 ChannelSearchManager::getTimeAtLastSend()
{
	Lock guard(_volMutex);
	int64 retval = _timeAtLastSend;
	return retval;
}

}}

