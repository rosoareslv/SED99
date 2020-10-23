// Copyright (c) 2014-2016 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

//#define ENABLE_DASH_DEBUG

#include "governance-classes.h"

#include "core_io.h"
#include "main.h"
#include "init.h"
#include "chainparams.h"

#include "consensus/consensus.h"
#include "consensus/merkle.h"
#include "consensus/validation.h"

#include "governance.h"
#include "masternode.h"
#include "governance.h"
#include <boost/lexical_cast.hpp>
#include <univalue.h>

#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/foreach.hpp>

using namespace std;

class CNode;

// DECLARE GLOBAL VARIABLES FOR GOVERNANCE CLASSES
CGovernanceTriggerManager triggerman;

// SPLIT UP STRING BY DELIMITER

/*  
    NOTE : SplitBy can be simplified via:
    http://www.boost.org/doc/libs/1_58_0/doc/html/boost/algorithm/split_idp202406848.html
*/

std::vector<std::string> SplitBy(std::string strCommand, std::string strDelimit)
{
    std::vector<std::string> vParts;
    boost::split(vParts, strCommand, boost::is_any_of(strDelimit));

    for(int q=0; q<(int)vParts.size(); q++) {
        if(strDelimit.find(vParts[q]) != std::string::npos) {
            vParts.erase(vParts.begin()+q);
            --q;
        }
    }

   return vParts;
}

CAmount ParsePaymentAmount(const std::string& strAmount)
{
    DBG( cout << "ParsePaymentAmount Start: strAmount = " << strAmount << endl; );

    CAmount nAmount = 0;
    if (strAmount.empty()) {
        std::ostringstream ostr;
        ostr << "ParsePaymentAmount: Amount is empty";
        throw runtime_error(ostr.str());
    }
    if(strAmount.size() > 20) {
        // String is much too long, the functions below impose stricter
        // requirements
        std::ostringstream ostr;
        ostr << "ParsePaymentAmount: Amount string too long";
        throw runtime_error(ostr.str());
    }
    // Make sure the string makes sense as an amount
    // Note: No spaces allowed
    // Also note: No scientific notation
    size_t pos = strAmount.find_first_not_of("0123456789.");
    if (pos != std::string::npos) {
        std::ostringstream ostr;
        ostr << "ParsePaymentAmount: Amount string contains invalid character";
        throw runtime_error(ostr.str());
    }

    pos = strAmount.find(".");
    if (pos == 0)  {
        // JSON doesn't allow values to start with a decimal point
        std::ostringstream ostr;
        ostr << "ParsePaymentAmount: Invalid amount string, leading decimal point not allowed";
        throw runtime_error(ostr.str());
    }

    // Make sure there's no more than 1 decimal point
    if ((pos != std::string::npos) && (strAmount.find(".", pos+1) != std::string::npos)) {
        std::ostringstream ostr;
        ostr << "ParsePaymentAmount: Invalid amount string, too many decimal points";
        throw runtime_error(ostr.str());
    }

    // Note this code is taken from AmountFromValue in rpcserver.cpp
    // which is used for parsing the amounts in createrawtransaction.
    if (!ParseFixedPoint(strAmount, 8, &nAmount)) {
        nAmount = 0;
        std::ostringstream ostr;
        ostr << "ParsePaymentAmount: ParseFixedPoint failed for string: " << strAmount;
        throw runtime_error(ostr.str());
    }
    if (!MoneyRange(nAmount)) {
        nAmount = 0;
        std::ostringstream ostr;
        ostr << "ParsePaymentAmount: Invalid amount string, value outside of valid money range";
        throw runtime_error(ostr.str());
    }

    DBG( cout << "ParsePaymentAmount Returning true nAmount = " << nAmount << endl; );

    return nAmount;
}

/**
*   Add Governance Object
*/

bool CGovernanceTriggerManager::AddNewTrigger(uint256 nHash)
{
    DBG( cout << "CGovernanceTriggerManager::AddNewTrigger: Start" << endl; );
    AssertLockHeld(governance.cs);

    // IF WE ALREADY HAVE THIS HASH, RETURN
    if(mapTrigger.count(nHash))  {
        DBG( 
            cout << "CGovernanceTriggerManager::AddNewTrigger: Already have hash"
                 << ", nHash = " << nHash.GetHex()
                 << ", count = " << mapTrigger.count(nHash)
                 << ", mapTrigger.size() = " << mapTrigger.size()
                 << endl; );
        return false;
    }

    CSuperblock_sptr superblock;
    try  {
        CSuperblock_sptr superblockTmp(new CSuperblock(nHash));
        superblock = superblockTmp;
    }
    catch(std::exception& e)  {
        DBG( cout << "CGovernanceTriggerManager::AddNewTrigger Error creating superblock"
             << ", e.what() = " << e.what()
             << endl; );
        LogPrintf("CGovernanceTriggerManager::AddNewTrigger: Error creating superblock: %s\n", e.what());
        return false;
    }
    catch(...)  {
        LogPrintf("CGovernanceTriggerManager::AddNewTrigger: Unknown Error creating superblock\n");
        DBG( cout << "CGovernanceTriggerManager::AddNewTrigger Error creating superblock catchall" << endl; );
        return false;
    }

    superblock->SetStatus(SEEN_OBJECT_IS_VALID);

    DBG( cout << "CGovernanceTriggerManager::AddNewTrigger: Inserting trigger" << endl; );
    mapTrigger.insert(make_pair(nHash, superblock));

    DBG( cout << "CGovernanceTriggerManager::AddNewTrigger: End" << endl; );

    return true;
}

/**
*
*   Clean And Remove
*
*/

void CGovernanceTriggerManager::CleanAndRemove()
{
    DBG( cout << "CGovernanceTriggerManager::CleanAndRemove: Start" << endl; );
    AssertLockHeld(governance.cs);

    // LOOK AT THESE OBJECTS AND COMPILE A VALID LIST OF TRIGGERS
    for(trigger_m_it it = mapTrigger.begin(); it != mapTrigger.end(); ++it)  {
        //int nNewStatus = -1;
        CGovernanceObject* pObj = governance.FindGovernanceObject((*it).first);
        if(!pObj)  {
            continue;
        }
        CSuperblock_sptr& superblock = it->second;
        if(!superblock)  {
            continue;
        }
        // IF THIS ISN'T A TRIGGER, WHY ARE WE HERE?
        if(pObj->GetObjectType() != GOVERNANCE_OBJECT_TRIGGER) {
            superblock->SetStatus(SEEN_OBJECT_ERROR_INVALID);
        }
    }

    // Remove triggers that are invalid or already executed
    DBG( cout << "CGovernanceTriggerManager::CleanAndRemove: mapTrigger.size() = " << mapTrigger.size() << endl; );
    trigger_m_it it = mapTrigger.begin();
    while(it != mapTrigger.end())  {
        bool remove = false;
        CSuperblock_sptr& superblock = it->second;
        if(!superblock)  {
            DBG( cout << "CGovernanceTriggerManager::CleanAndRemove: NULL superblock marked for removal " << endl; );
            remove = true;
        }
        else  {
            DBG( cout << "CGovernanceTriggerManager::CleanAndRemove: superblock status = " << superblock->GetStatus() << endl; );
            switch(superblock->GetStatus())  {
            case SEEN_OBJECT_ERROR_INVALID:
            case SEEN_OBJECT_UNKNOWN:
                remove = true;
                break;
            case SEEN_OBJECT_EXECUTED:
                {
                    CGovernanceObject* govobj = superblock->GetGovernanceObject();
                    if(govobj)  {
                        govobj->fExpired = true;
                    }
                }
                remove = true;
                break;
            case SEEN_OBJECT_IS_VALID:
                {
                    // Rough approximation: 30 days per month * 576 blocks per day
                    static const int nMonthlyBlocks = 30*576;
                    int nTriggerBlock = superblock->GetBlockStart();
                    int nExpirationBlock = nTriggerBlock + nMonthlyBlocks;
                    if(governance.GetCachedBlockHeight() > nExpirationBlock)  {
                        remove = true;
                        CGovernanceObject* govobj = superblock->GetGovernanceObject();
                        if(govobj)  {
                            govobj->fExpired = true;
                        }
                    }
                }
                break;
            default:
                break;
            }
        }
        
        if(remove)  {
            DBG( 
                string strdata = "NULL";
                CGovernanceObject* govobj = superblock->GetGovernanceObject();
                if(govobj)  {
                    strdata = govobj->GetDataAsString();
                }
                cout << "CGovernanceTriggerManager::CleanAndRemove: Removing object: " 
                     << strdata
                     << endl;
               );
            mapTrigger.erase(it++);
        }
        else  {
            ++it;
        }
    }

    DBG( cout << "CGovernanceTriggerManager::CleanAndRemove: End" << endl; );
}

/**
*   Get Active Triggers
*
*   - Look through triggers and scan for active ones
*   - Return the triggers in a list
*/

std::vector<CSuperblock_sptr> CGovernanceTriggerManager::GetActiveTriggers()
{
    AssertLockHeld(governance.cs);
    std::vector<CSuperblock_sptr> vecResults;

    DBG( cout << "GetActiveTriggers: mapTrigger.size() = " << mapTrigger.size() << endl; );

    // LOOK AT THESE OBJECTS AND COMPILE A VALID LIST OF TRIGGERS
    trigger_m_it it = mapTrigger.begin();
    while(it != mapTrigger.end()) {

        CGovernanceObject* pObj = governance.FindGovernanceObject((*it).first);

        if(pObj) {
            DBG( cout << "GetActiveTriggers: pObj->GetDataAsString() = " << pObj->GetDataAsString() << endl; );
            vecResults.push_back(it->second);
        }
        ++it;
    }

    DBG( cout << "GetActiveTriggers: vecResults.size() = " << vecResults.size() << endl; );

    return vecResults;
}

/**
*   Is Superblock Triggered
*
*   - Does this block have a non-executed and actived trigger?
*/

bool CSuperblockManager::IsSuperblockTriggered(int nBlockHeight)
{
    if (!CSuperblock::IsValidBlockHeight(nBlockHeight)) {
        return false;
    }

    LOCK(governance.cs);
    // GET ALL ACTIVE TRIGGERS
    std::vector<CSuperblock_sptr> vecTriggers = triggerman.GetActiveTriggers();

    DBG( cout << "IsSuperblockTriggered Number triggers = " << vecTriggers.size() << endl; );

    BOOST_FOREACH(CSuperblock_sptr superblock, vecTriggers)
    {
        if(!superblock)  {
            DBG( cout << "IsSuperblockTriggered Not a superblock, continuing " << endl; );
            continue;
        }

        CGovernanceObject* pObj = superblock->GetGovernanceObject();

        if(!pObj)  {
            DBG( cout << "IsSuperblockTriggered pObj is NULL, continuing" << endl; );
            continue;
        }

        // note : 12.1 - is epoch calculation correct?

        if(nBlockHeight != superblock->GetBlockStart()) {
            DBG( cout << "IsSuperblockTriggered Not the target block, continuing" 
                      << ", nBlockHeight = " << nBlockHeight
                      << ", superblock->GetBlockStart() = " << superblock->GetBlockStart()
                      << endl; );
            continue;
        }

        // MAKE SURE THIS TRIGGER IS ACTIVE VIA FUNDING CACHE FLAG

        if(pObj->fCachedFunding)  {
            DBG( cout << "IsSuperblockTriggered returning true" << endl; );
            return true;
        }       
        else  {
            DBG( cout << "IsSuperblockTriggered No fCachedFunding, continuing" << endl; );
        }
    }

    return false;
}


bool CSuperblockManager::GetBestSuperblock(CSuperblock_sptr& pBlock, int nBlockHeight)
{
    if(!CSuperblock::IsValidBlockHeight(nBlockHeight)) {
        return false;
    }

    AssertLockHeld(governance.cs);
    std::vector<CSuperblock_sptr> vecTriggers = triggerman.GetActiveTriggers();
    int nYesCount = 0;

    BOOST_FOREACH(CSuperblock_sptr superblock, vecTriggers) {
        if(!superblock) {
            DBG( cout << "GetBestSuperblock Not a superblock, continuing" << endl; );
            continue;
        }

        CGovernanceObject* pObj = superblock->GetGovernanceObject();

        if(!pObj)  {
            DBG( cout << "GetBestSuperblock pObj is NULL, continuing" << endl; );
            continue;
        }

        if(nBlockHeight != superblock->GetBlockStart()) {
            DBG( cout << "GetBestSuperblock Not the target block, continuing" << endl; );
            continue;
        }

        // DO WE HAVE A NEW WINNER?

        int nTempYesCount = pObj->GetAbsoluteYesCount(VOTE_SIGNAL_FUNDING);
        DBG( cout << "GetBestSuperblock nTempYesCount = " << nTempYesCount << endl; );
        if(nTempYesCount > nYesCount)  {
            nYesCount = nTempYesCount;
            pBlock = superblock;
            DBG( cout << "GetBestSuperblock Valid superblock found, pBlock set" << endl; );
        }
    }

    return nYesCount > 0;
}

/**
*   Create Superblock Payments
*
*   - Create the correct payment structure for a given superblock
*/

void CSuperblockManager::CreateSuperblock(CMutableTransaction& txNewRet, int nBlockHeight, std::vector<CTxOut>& voutSuperblockRet)
{
    DBG( cout << "CSuperblockManager::CreateSuperblock Start" << endl; );

    LOCK(governance.cs);

    // GET THE BEST SUPERBLOCK FOR THIS BLOCK HEIGHT

    CSuperblock_sptr pBlock;
    if(!CSuperblockManager::GetBestSuperblock(pBlock, nBlockHeight))  {
        LogPrint("superblock", "CSuperblockManager::CreateSuperblock: Can't find superblock for height %d\n", nBlockHeight);
        DBG( cout << "CSuperblockManager::CreateSuperblock Failed to get superblock for height, returning" << endl; );
        return;
    }

    // make sure it's empty, just in case
    voutSuperblockRet.clear();

    // CONFIGURE SUPERBLOCK OUTPUTS

    // Superblock payments are appended to the end of the coinbase vout vector
    DBG( cout << "CSuperblockManager::CreateSuperblock Number payments: " << pBlock->CountPayments() << endl; );

    // TODO: How many payments can we add before things blow up?
    //       Consider at least following limits:
    //          - max coinbase tx size
    //          - max "budget" available
    for(int i = 0; i < pBlock->CountPayments(); i++)  {
        CGovernancePayment payment;
        DBG( cout << "CSuperblockManager::CreateSuperblock i = " << i << endl; );
        if(pBlock->GetPayment(i, payment))  {
            DBG( cout << "CSuperblockManager::CreateSuperblock Payment found " << endl; );
            // SET COINBASE OUTPUT TO SUPERBLOCK SETTING

            CTxOut txout = CTxOut(payment.nAmount, payment.script);
            txNewRet.vout.push_back(txout);
            voutSuperblockRet.push_back(txout);

            // PRINT NICE LOG OUTPUT FOR SUPERBLOCK PAYMENT

            CTxDestination address1;
            ExtractDestination(payment.script, address1);
            CBitcoinAddress address2(address1);

            // TODO: PRINT NICE N.N DASH OUTPUT

            DBG( cout << "CSuperblockManager::CreateSuperblock Before LogPrintf call, nAmount = " << payment.nAmount << endl; );
            LogPrintf("NEW Superblock : output %d (addr %s, amount %d)\n", i, address2.ToString(), payment.nAmount);
            DBG( cout << "CSuperblockManager::CreateSuperblock After LogPrintf call " << endl; );
            pBlock->SetExecuted();
        }
        else  {
            DBG( cout << "CSuperblockManager::CreateSuperblock Payment not found " << endl; );
        }
    }

    DBG( cout << "CSuperblockManager::CreateSuperblock End" << endl; );
}

bool CSuperblockManager::IsValid(const CTransaction& txNew, int nBlockHeight, CAmount blockReward)
{
    // GET BEST SUPERBLOCK, SHOULD MATCH
    LOCK(governance.cs);

    CSuperblock_sptr pSuperblock;
    if(CSuperblockManager::GetBestSuperblock(pSuperblock, nBlockHeight)) {
        return pSuperblock->IsValid(txNew, nBlockHeight, blockReward);
    }

    return false;
}

CSuperblock::
CSuperblock()
    : nGovObjHash(),
      nEpochStart(0),
      nStatus(SEEN_OBJECT_UNKNOWN),
      vecPayments()
{}

CSuperblock::
CSuperblock(uint256& nHash)
    : nGovObjHash(nHash),
      nEpochStart(0),
      nStatus(SEEN_OBJECT_UNKNOWN),
      vecPayments()
{
    DBG( cout << "CSuperblock Constructor Start" << endl; );
    
    CGovernanceObject* pGovObj = GetGovernanceObject();
    
    if(!pGovObj) {
        DBG( cout << "CSuperblock Constructor pGovObjIn is NULL, returning" << endl; );
        throw runtime_error("CSuperblock: Failed to find Governance Object");
    }
    
    DBG( cout << "CSuperblock Constructor pGovObj : "
         << pGovObj->GetDataAsString()
         << ", nObjectType = " << pGovObj->nObjectType
         << endl; );
    
    if (pGovObj->GetObjectType() != GOVERNANCE_OBJECT_TRIGGER) {
        DBG( cout << "CSuperblock Constructor pHoObj not a trigger, returning" << endl; );
        throw runtime_error("CSuperblock: Governance Object not a trigger");
    }

    UniValue obj = pGovObj->GetJSONObject();
        
    // FIRST WE GET THE START EPOCH, THE DATE WHICH THE PAYMENT SHALL OCCUR
    nEpochStart = obj["event_block_height"].get_int();

    // NEXT WE GET THE PAYMENT INFORMATION AND RECONSTRUCT THE PAYMENT VECTOR
    std::string strAddresses = obj["payment_addresses"].get_str();
    std::string strAmounts = obj["payment_amounts"].get_str();
    ParsePaymentSchedule(strAddresses, strAmounts);
    
    DBG( cout << "CSuperblock Constructor End" << endl; );
}

/**
 *   Is Valid Superblock Height
 *
 *   - See if a block at this height can be a superblock
 */

bool CSuperblock::IsValidBlockHeight(int nBlockHeight)
{
    // SUPERBLOCKS CAN HAPPEN ONLY after hardfork and only ONCE PER CYCLE
    return nBlockHeight >= Params().GetConsensus().nSuperblockStartBlock &&
            ((nBlockHeight % Params().GetConsensus().nSuperblockCycle) == 0);
}

CAmount CSuperblock::GetPaymentsLimit(int nBlockHeight)
{
    const Consensus::Params& consensusParams = Params().GetConsensus();

    if(!IsValidBlockHeight(nBlockHeight)) {
        return 0;
    }

    // min subsidy for high diff networks and vice versa
    int nBits = consensusParams.fPowAllowMinDifficultyBlocks ? UintToArith256(consensusParams.powLimit).GetCompact() : 1;
    // some part of all blocks issued during the cycle goes to superblock, see GetBlockSubsidy
    CAmount nSuperblockPartOfSubsidy = GetBlockSubsidy(nBits, nBlockHeight, consensusParams, true);
    CAmount nPaymentsLimit = nSuperblockPartOfSubsidy * consensusParams.nSuperblockCycle;
    LogPrint("gobject", "CSuperblock::GetPaymentsLimit -- Valid superblock height %d, payments max %lld\n", nBlockHeight, nPaymentsLimit);

    return nPaymentsLimit;
}

void CSuperblock::ParsePaymentSchedule(std::string& strPaymentAddresses, std::string& strPaymentAmounts)
{
    // SPLIT UP ADDR/AMOUNT STRINGS AND PUT IN VECTORS

    std::vector<std::string> vecParsed1;
    std::vector<std::string> vecParsed2;
    vecParsed1 = SplitBy(strPaymentAddresses, "|");
    vecParsed2 = SplitBy(strPaymentAmounts, "|");

    // IF THESE DONT MATCH, SOMETHING IS WRONG

    if (vecParsed1.size() != vecParsed2.size()) {
        std::ostringstream ostr;
        ostr << "CSuperblock::ParsePaymentSchedule Mismatched payments and amounts";
        throw std::runtime_error(ostr.str());
    }

    if (vecParsed1.size() == 0) {
        std::ostringstream ostr;
        ostr << "CSuperblock::ParsePaymentSchedule Error no payments";
        throw std::runtime_error(ostr.str());
    }

    // LOOP THROUGH THE ADDRESSES/AMOUNTS AND CREATE PAYMENTS
    /*
      ADDRESSES = [ADDR1|2|3|4|5\6]
      AMOUNTS = [AMOUNT1|2|3|4|5\6]
    */

    DBG( cout << "CSuperblock::ParsePaymentSchedule vecParsed1.size() = " << vecParsed1.size() << endl; );

    for (int i = 0; i < (int)vecParsed1.size(); i++) {
        CBitcoinAddress address(vecParsed1[i]);
        if (!address.IsValid())  {
            std::ostringstream ostr;
            ostr << "CSuperblock::ParsePaymentSchedule Invalid Dash Address : " <<  vecParsed1[i];
            throw std::runtime_error(ostr.str());
        }

        DBG( cout << "CSuperblock::ParsePaymentSchedule i = " << i 
             <<  ", vecParsed2[i] = " << vecParsed2[i]
             << endl; );

        CAmount nAmount = ParsePaymentAmount(vecParsed2[i]);

        DBG( cout << "CSuperblock::ParsePaymentSchedule: "
             << "amount string = " << vecParsed2[i]
             << ", nAmount = " << nAmount
             << endl; );

        CGovernancePayment payment(address, nAmount);
        if(payment.IsValid()) {
            vecPayments.push_back(payment);
        }
    }
}

bool CSuperblock::GetPayment(int nPaymentIndex, CGovernancePayment& paymentRet)
{
    if((nPaymentIndex<0) || (nPaymentIndex >= (int)vecPayments.size())) {
        return false;
    }

    paymentRet = vecPayments[nPaymentIndex];
    return true;
}

CAmount CSuperblock::GetPaymentsTotalAmount()
{
    CAmount nPaymentsTotalAmount = 0;
    int nPayments = CountPayments();

    for(int i = 0; i < nPayments; i++) {
        nPaymentsTotalAmount += vecPayments[i].nAmount;
    }

    return nPaymentsTotalAmount;
}

/**
*   Is Transaction Valid
*
*   - Does this transaction match the superblock?
*/

bool CSuperblock::IsValid(const CTransaction& txNew, int nBlockHeight, CAmount blockReward)
{
    // TODO : LOCK(cs);
    // No reason for a lock here now since this method only accesses data
    // internal to *this and since CSuperblock's are accessed only through
    // shared pointers there's no way our object can get deleted while this
    // code is running.
    if(!IsValidBlockHeight(nBlockHeight)) {
        LogPrintf("CSuperblock::IsValid -- ERROR: Block invalid, incorrect block height\n");
        return false;
    }

    std::string strPayeesPossible = "";

    // CONFIGURE SUPERBLOCK OUTPUTS 

    int nOutputs = txNew.vout.size();
    int nPayments = CountPayments();
    int nMinerPayments = nOutputs - nPayments;

    // We require an exact match (including order) between the expected
    // superblock payments and the payments actually in the block, after
    // skipping any initial miner payments.

    if(nMinerPayments < 0) {
        // This means the block cannot have all the superblock payments
        // so it is not valid.
        // TODO: could that be that we just hit coinbase size limit?
        LogPrintf("CSuperblock::IsValid -- ERROR: Block invalid, too few superblock payments\n");
        return false;
    }

    // payments should not exceed limit
    CAmount nPaymentsTotalAmount = GetPaymentsTotalAmount();
    CAmount nPaymentsLimit = GetPaymentsLimit(nBlockHeight);
    if(nPaymentsTotalAmount > nPaymentsLimit) {
        LogPrintf("CSuperblock::IsValid -- ERROR: Block invalid, payments limit exceeded: payments %lld, limit %lld\n", nPaymentsTotalAmount, nPaymentsLimit);
        return false;
    }

    // miner should not get more than he would usually get
    CAmount nBlockValue = txNew.GetValueOut();
    if(nBlockValue > blockReward + nPaymentsTotalAmount) {
        LogPrintf("CSuperblock::IsValid -- ERROR: Block invalid, block value limit exceeded: block %lld, limit %lld\n", nBlockValue, blockReward + nPaymentsTotalAmount);
        return false;
    }

    for(int i = 0; i < nPayments; i++) {
        CGovernancePayment payment;
        if(!GetPayment(i, payment)) {
            // This shouldn't happen so log a warning
            LogPrintf("CSuperblock::IsValid -- WARNING: Failed to find payment: %d of %d total payments\n", i, nPayments);
            continue;
        }

        int nVoutIndex = nMinerPayments + i;

        bool fPaymentMatch = ((payment.script == txNew.vout[nVoutIndex].scriptPubKey) &&
                              (payment.nAmount == txNew.vout[nVoutIndex].nValue));

        if(!fPaymentMatch) {
            // MISMATCHED SUPERBLOCK OUTPUT!

            CTxDestination address1;
            ExtractDestination(payment.script, address1);
            CBitcoinAddress address2(address1);
            LogPrintf("CSuperblock::IsValid -- WARNING: Block invalid: output n %d payment %d to %s\n", nVoutIndex, payment.nAmount, address2.ToString());

            return false;
        }
    }

    return true;
}

/**
*   Get Required Payment String
*
*   - Get a string representing the payments required for a given superblock
*/

std::string CSuperblockManager::GetRequiredPaymentsString(int nBlockHeight)
{
    LOCK(governance.cs);
    std::string ret = "Unknown";

    // GET BEST SUPERBLOCK

    CSuperblock_sptr pBlock;
    if(!CSuperblockManager::GetBestSuperblock(pBlock, nBlockHeight)) {
        LogPrint("superblock", "CSuperblockManager::CreateSuperblock: Can't find superblock for height %d\n", nBlockHeight);
        return "error";
    }

    // LOOP THROUGH SUPERBLOCK PAYMENTS, CONFIGURE OUTPUT STRING 

    for(int i = 0; i < pBlock->CountPayments(); i++) {
        CGovernancePayment payment;
        if(pBlock->GetPayment(i, payment)) {
            // PRINT NICE LOG OUTPUT FOR SUPERBLOCK PAYMENT

            CTxDestination address1;
            ExtractDestination(payment.script, address1);
            CBitcoinAddress address2(address1);

            // RETURN NICE OUTPUT FOR CONSOLE

            if(ret != "Unknown") {
                ret += ", " + address2.ToString();
            }
            else {
                ret = address2.ToString();
            }
        }
    }

    return ret;
}
