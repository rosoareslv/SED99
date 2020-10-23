// Copyright (c) 2009-2012 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SPORK_H
#define SPORK_H

#include "hash.h"
#include "net.h"
#include "utilstrencodings.h"

class CSporkMessage;
class CSporkManager;

/*
    Don't ever reuse these IDs for other sporks
    - This would result in old clients getting confused about which spork is for what
*/
static const int SPORK_START                                            = 10001;
static const int SPORK_END                                              = 10012;

static const int SPORK_2_INSTANTX                                       = 10001;
static const int SPORK_3_INSTANTX_BLOCK_FILTERING                       = 10002;
static const int SPORK_5_MAX_VALUE                                      = 10004;
static const int SPORK_7_MASTERNODE_SCANNING                            = 10006;
static const int SPORK_8_MASTERNODE_PAYMENT_ENFORCEMENT                 = 10007;
static const int SPORK_9_SUPERBLOCKS_ENABLED                            = 10008;
static const int SPORK_10_MASTERNODE_PAY_UPDATED_NODES                  = 10009;
static const int SPORK_12_RECONSIDER_BLOCKS                             = 10011;
static const int SPORK_13_OLD_SUPERBLOCK_FLAG                           = 10012;

static const int64_t SPORK_2_INSTANTX_DEFAULT                           = 978307200;    //2001-1-1
static const int64_t SPORK_3_INSTANTX_BLOCK_FILTERING_DEFAULT           = 1424217600;   //2015-2-18
static const int64_t SPORK_5_MAX_VALUE_DEFAULT                          = 1000;         //1000 DASH
static const int64_t SPORK_7_MASTERNODE_SCANNING_DEFAULT                = 978307200;    //2001-1-1
static const int64_t SPORK_8_MASTERNODE_PAYMENT_ENFORCEMENT_DEFAULT     = 4070908800;   //OFF
static const int64_t SPORK_9_SUPERBLOCKS_ENABLED_DEFAULT                = 4070908800;   //OFF
static const int64_t SPORK_10_MASTERNODE_PAY_UPDATED_NODES_DEFAULT      = 4070908800;   //OFF
static const int64_t SPORK_12_RECONSIDER_BLOCKS_DEFAULT                 = 0;
static const int64_t SPORK_13_OLD_SUPERBLOCK_FLAG_DEFAULT               = 4070908800;   //OFF

extern std::map<uint256, CSporkMessage> mapSporks;
extern CSporkManager sporkManager;

//
// Spork classes
// Keep track of all of the network spork settings
//

class CSporkMessage
{
private:
    std::vector<unsigned char> vchSig;

public:
    int nSporkID;
    int64_t nValue;
    int64_t nTimeSigned;

    CSporkMessage(int nSporkID, int64_t nValue, int64_t nTimeSigned) :
        nSporkID(nSporkID),
        nValue(nValue),
        nTimeSigned(nTimeSigned)
        {}

    CSporkMessage() :
        nSporkID(0),
        nValue(0),
        nTimeSigned(0)
        {}


    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(nSporkID);
        READWRITE(nValue);
        READWRITE(nTimeSigned);
        READWRITE(vchSig);
    }

    uint256 GetHash() const
    {
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << nSporkID;
        ss << nValue;
        ss << nTimeSigned;
        return ss.GetHash();
    }

    bool Sign(std::string strSignKey);
    bool CheckSignature();
    void Relay();
};


class CSporkManager
{
private:
    std::vector<unsigned char> vchSig;
    std::string strMasterPrivKey;
    std::map<int, CSporkMessage> mapSporksActive;

public:

    CSporkManager() {}

    void ProcessSpork(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);
    void ExecuteSpork(int nSporkID, int nValue);
    bool UpdateSpork(int nSporkID, int64_t nValue);

    bool IsSporkActive(int nSporkID);
    int64_t GetSporkValue(int nSporkID);
    int GetSporkIDByName(std::string strName);
    std::string GetSporkNameByID(int nSporkID);

    bool SetPrivKey(std::string strPrivKey);
};

#endif
