#include "offer.h"
#include "alias.h"
#include "escrow.h"
#include "cert.h"
#include "message.h"
#include "init.h"
#include "main.h"
#include "util.h"
#include "random.h"
#include "base58.h"
#include "rpcserver.h"
#include "wallet/wallet.h"
#include "consensus/validation.h"
#include "chainparams.h"
#include <boost/algorithm/hex.hpp>
#include <boost/xpressive/xpressive_dynamic.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/foreach.hpp>
#include <boost/thread.hpp>
using namespace std;
extern void SendMoney(const CTxDestination &address, CAmount nValue, bool fSubtractFeeFromAmount, CWalletTx& wtxNew);
extern void SendMoneySyscoin(const vector<CRecipient> &vecSend, CAmount nValue, bool fSubtractFeeFromAmount, CWalletTx& wtxNew, const CWalletTx* wtxInOffer=NULL, const CWalletTx* wtxInCert=NULL, const CWalletTx* wtxInAlias=NULL, const CWalletTx* wtxInEscrow=NULL, bool syscoinTx=true);
bool DisconnectAlias(const CBlockIndex *pindex, const CTransaction &tx, int op, vector<vector<unsigned char> > &vvchArgs );
bool DisconnectOffer(const CBlockIndex *pindex, const CTransaction &tx, int op, vector<vector<unsigned char> > &vvchArgs );
bool DisconnectCertificate(const CBlockIndex *pindex, const CTransaction &tx, int op, vector<vector<unsigned char> > &vvchArgs );
bool DisconnectMessage(const CBlockIndex *pindex, const CTransaction &tx, int op, vector<vector<unsigned char> > &vvchArgs );
bool DisconnectEscrow(const CBlockIndex *pindex, const CTransaction &tx, int op, vector<vector<unsigned char> > &vvchArgs );

// check wallet transactions to see if there was a refund for an accept already
// need this because during a reorg blocks are disconnected (deleted from db) and we can't rely on looking in db to see if refund was made for an accept
bool foundRefundInWallet(const vector<unsigned char> &vchAcceptRand, const vector<unsigned char>& acceptCode)
{
    TRY_LOCK(pwalletMain->cs_wallet, cs_trylock);
    BOOST_FOREACH(PAIRTYPE(const uint256, CWalletTx)& item, pwalletMain->mapWallet)
    {
		vector<vector<unsigned char> > vvchArgs;
		int op, nOut;
        const CWalletTx& wtx = item.second;
        if (wtx.IsCoinBase() || !CheckFinalTx(wtx))
            continue;
		if (DecodeOfferTx(wtx, op, nOut, vvchArgs))
		{
			if(op == OP_OFFER_REFUND)
			{
				if(vvchArgs.size() == 3 && vchAcceptRand == vvchArgs[1] && vvchArgs[2] == acceptCode)
				{
					return true;
				}
			}
		}
	}
	return false;
}
bool foundOfferLinkInWallet(const vector<unsigned char> &vchOffer, const vector<unsigned char> &vchAcceptRandLink)
{
    TRY_LOCK(pwalletMain->cs_wallet, cs_trylock);

    BOOST_FOREACH(PAIRTYPE(const uint256, CWalletTx)& item, pwalletMain->mapWallet)
    {
		vector<vector<unsigned char> > vvchArgs;
		int op, nOut;
        const CWalletTx& wtx = item.second;
        if (wtx.IsCoinBase() || !CheckFinalTx(wtx))
            continue;
		if (DecodeOfferTx(wtx, op, nOut, vvchArgs))
		{
			if(op == OP_OFFER_ACCEPT)
			{
				if(vvchArgs.size() == 2 && vvchArgs[0] == vchOffer)
				{
					COffer theOffer(wtx);
					COfferAccept theOfferAccept = theOffer.accept;
					if (theOffer.IsNull() || theOfferAccept.IsNull())
						continue;
					if(theOfferAccept.vchAcceptRand == vvchArgs[1])
					{
						if(theOfferAccept.vchLinkOfferAccept == vchAcceptRandLink)
							return true;
					}
				}
			}
		}
	}
	return false;
}
// transfer cert if its linked to offer
string makeTransferCertTX(const COffer& theOffer, const COfferAccept& theOfferAccept)
{

	string strPubKey = stringFromVch(theOfferAccept.vchBuyerKey);
	string strError;
	string strMethod = string("certtransfer");
	UniValue params(UniValue::VARR);

	
	params.push_back(stringFromVch(theOffer.vchCert));
	params.push_back(strPubKey);	
    try {
        tableRPC.execute(strMethod, params);
	}
	catch (UniValue& objError)
	{
		return find_value(objError, "message").get_str().c_str();
	}
	catch(std::exception& e)
	{
		return string(e.what()).c_str();
	}
	return "";

}
// refund an offer accept by creating a transaction to send coins to offer accepter, and an offer accept back to the offer owner. 2 Step process in order to use the coins that were sent during initial accept.
string makeOfferLinkAcceptTX(const COfferAccept& theOfferAccept, const vector<unsigned char> &vchOffer, const vector<unsigned char> &vchOfferAcceptLink)
{
	string strError;
	string strMethod = string("offeraccept");
	UniValue params(UniValue::VARR);

	CPubKey newDefaultKey;
	
	if(foundOfferLinkInWallet(vchOffer, vchOfferAcceptLink))
	{
		if(fDebug)
			LogPrintf("makeOfferLinkAcceptTX() offer linked transaction already exists\n");
		return "";
	}
	if(!theOfferAccept.txBTCId.IsNull())
	{
		if(fDebug)
			LogPrintf("makeOfferLinkAcceptTX() cannot accept a linked offer by paying in Bitcoins\n");
		return "";
	}


	params.push_back(stringFromVch(vchOffer));
	params.push_back(static_cast<ostringstream*>( &(ostringstream() << theOfferAccept.nQty) )->str());
	params.push_back(stringFromVch(theOfferAccept.vchMessage));
	params.push_back("");
	params.push_back("");
	params.push_back(stringFromVch(vchOffer));
	params.push_back(stringFromVch(vchOfferAcceptLink));
	params.push_back("");
	
    try {
        tableRPC.execute(strMethod, params);
	}
	catch (UniValue& objError)
	{
		return find_value(objError, "message").get_str().c_str();
	}
	catch(std::exception& e)
	{
		return string(e.what()).c_str();
	}
	return "";

}
// refund an offer accept by creating a transaction to send coins to offer accepter, and an offer accept back to the offer owner. 2 Step process in order to use the coins that were sent during initial accept.
string makeOfferRefundTX(const CTransaction& prevTx, const vector<unsigned char> &vchOffer, const vector<unsigned char> &vchAcceptRand, const vector<unsigned char> &refundCode)
{
	CTransaction myPrevTx = prevTx;
	CTransaction myOfferAcceptTx, offerTx;
	COfferAccept theOfferAccept;
	if(GetTxOfOfferAccept(*pofferdb, vchOffer, vchAcceptRand, theOfferAccept, myOfferAcceptTx))
	{
		if(!IsSyscoinTxMine(myOfferAcceptTx))
			return string("makeOfferRefundTX(): cannot refund an accept of an offer that isn't mine");

		// check for existence of offeraccept in txn offer obj
		if(theOfferAccept.vchAcceptRand != vchAcceptRand)
			return string("makeOfferRefundTX(): cannot find accept in offer txn");

		if(theOfferAccept.nQty <= 0)
			return string("makeOfferRefundTX(): cannot refund an accept with 0 quantity");

	}
	else
		return string("makeOfferRefundTX(): cannot find accept transaction");
	if(myPrevTx.IsNull())
		myPrevTx = myOfferAcceptTx;
	if(!pwalletMain)
	{
		return string("makeOfferRefundTX(): no wallet found");
	}
	if(theOfferAccept.bRefunded)
	{
		return string("makeOfferRefundTX(): this offer accept has already been refunded");
	}	
	const CWalletTx *wtxPrevIn;
	wtxPrevIn = pwalletMain->GetWalletTx(myPrevTx.GetHash());
	if (wtxPrevIn == NULL)
	{
		return string("makeOfferRefundTX() : can't find this offer in your wallet");
	}

	COffer theOffer(myOfferAcceptTx);
	if(theOffer.IsNull())
		return string("makeOfferRefundTX(): could not decode offer");
	if (!GetTxOfOffer(*pofferdb, vchOffer, theOffer, offerTx))
		return string("makeOfferRefundTX(): could not find an offer with this name");

	// this is a syscoin txn
	CWalletTx wtx, wtx2;
	CAmount nTotalValue = 0;
	CScript scriptPubKeyOrig, scriptPayment;

	if(refundCode == OFFER_REFUND_COMPLETE)
	{
		int precision = 2;
		// lookup the price of the offer in syscoin based on pegged alias at the block # when accept was made (sets nHeight in offeraccept serialized UniValue in tx)
		nTotalValue = convertCurrencyCodeToSyscoin(theOffer.sCurrencyCode, theOfferAccept.nPrice, theOfferAccept.nHeight, precision)*theOfferAccept.nQty;
	} 
	else
	{
		if (!theOffer.vchLinkOffer.empty())
			string("makeOfferRefundTX() :You cannot refund an offer that is linked to another offer, only the owner of the original offer can issue a refund.");
	}


	// create OFFERACCEPT txn keys
	CScript scriptPubKey;
	std::vector<unsigned char> vchKeyByte;
	boost::algorithm::unhex(theOffer.vchPubKey.begin(), theOffer.vchPubKey.end(), std::back_inserter(vchKeyByte));
	CPubKey currentKey(vchKeyByte);
	scriptPubKeyOrig = GetScriptForDestination(currentKey.GetID());

	scriptPubKey << CScript::EncodeOP_N(OP_OFFER_REFUND) << vchOffer << vchAcceptRand << refundCode << OP_2DROP << OP_2DROP;
	scriptPubKey += scriptPubKeyOrig;
	
	if (ExistsInMempool(vchOffer, OP_OFFER_ACTIVATE) || ExistsInMempool(vchOffer, OP_OFFER_UPDATE)) {
		return string("makeOfferRefundTX(): there are pending operations or refunds on that offer");
	}

	if(foundRefundInWallet(vchAcceptRand, refundCode))
	{
		return string("makeOfferRefundTX(): foundRefundInWallet - This offer accept has already been refunded");
	}
    // add a copy of the offer with just
    // the one accept to save bandwidth
    COffer offerCopy = theOffer;
    COfferAccept offerAcceptCopy = theOfferAccept;
    offerCopy.ClearOffer();
	offerCopy.accept.vchAcceptRand = offerAcceptCopy.vchAcceptRand;

	vector<CRecipient> vecSend;
	CRecipient recipient;
	CreateRecipient(scriptPubKey, recipient);
	vecSend.push_back(recipient);
	const vector<unsigned char> &data = offerCopy.Serialize();
	CScript scriptData;
	scriptData << OP_RETURN << data;
	CRecipient fee;
	CreateFeeRecipient(scriptData, data, fee);
	vecSend.push_back(fee);
	const CWalletTx * wtxInCert=NULL;
	const CWalletTx * wtxInAlias=NULL;
	const CWalletTx * wtxInEscrow=NULL;
	SendMoneySyscoin(vecSend, recipient.nAmount+fee.nAmount, false, wtx, wtxPrevIn, wtxInCert, wtxInAlias, wtxInEscrow);
	
	if(refundCode == OFFER_REFUND_COMPLETE)
	{
		CSyscoinAddress refundAddress(stringFromVch(theOfferAccept.vchRefundAddress));
		SendMoney(refundAddress.Get(), nTotalValue, false, wtx2);
	}	
	return "";

}

bool IsOfferOp(int op) {
	return op == OP_OFFER_ACTIVATE
        || op == OP_OFFER_UPDATE
        || op == OP_OFFER_ACCEPT
		|| op == OP_OFFER_REFUND;
}


int GetOfferExpirationDepth() {
    return 525600;
}

string offerFromOp(int op) {
	switch (op) {
	case OP_OFFER_ACTIVATE:
		return "offeractivate";
	case OP_OFFER_UPDATE:
		return "offerupdate";
	case OP_OFFER_ACCEPT:
		return "offeraccept";
	case OP_OFFER_REFUND:
		return "offerrefund";
	default:
		return "<unknown offer op>";
	}
}
bool COffer::UnserializeFromTx(const CTransaction &tx) {
	vector<unsigned char> vchData;
	if(!GetSyscoinData(tx, vchData))
		return false;
    try {
        CDataStream dsOffer(vchData, SER_NETWORK, PROTOCOL_VERSION);
        dsOffer >> *this;
    } catch (std::exception &e) {
        return false;
    }
    return true;
}
const vector<unsigned char> COffer::Serialize() {
    CDataStream dsOffer(SER_NETWORK, PROTOCOL_VERSION);
    dsOffer << *this;
    const vector<unsigned char> vchData(dsOffer.begin(), dsOffer.end());
    return vchData;

}
//TODO implement
bool COfferDB::ScanOffers(const std::vector<unsigned char>& vchOffer, unsigned int nMax,
		std::vector<std::pair<std::vector<unsigned char>, COffer> >& offerScan) {

	boost::scoped_ptr<CDBIterator> pcursor(NewIterator());
	pcursor->Seek(make_pair(string("offeri"), vchOffer));
    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
		pair<string, vector<unsigned char> > key;
        try {
			if (pcursor->GetKey(key) && key.first == "offeri") {
            	vector<unsigned char> vchOffer = key.second;
                vector<COffer> vtxPos;
				pcursor->GetValue(vtxPos);
                COffer txPos;
                if (!vtxPos.empty())
                    txPos = vtxPos.back();
                offerScan.push_back(make_pair(vchOffer, txPos));
            }
            if (offerScan.size() >= nMax)
                break;

            pcursor->Next();
        } catch (std::exception &e) {
            return error("%s() : deserialize error", __PRETTY_FUNCTION__);
        }
    }
    return true;
}

/**
 * [ReconstructSyscoinServicesIndex description]
 * @param  pindexRescan [description]
 * @return              [description]
 */
void ReconstructSyscoinServicesIndex(CBlockIndex *pindexRescan) {
    CBlockIndex* pindex = pindexRescan;  
	CValidationState state;
	bool fBlock = true;
	bool fMiner = false;
	bool bCheckInputs = false;
	CCoinsViewCache inputs(pcoinsTip);
	while (pindex) {  

		int nHeight = pindex->nHeight;
		CBlock block;
		ReadBlockFromDisk(block, pindex, Params().GetConsensus());
		uint256 txblkhash;
		// undo syscoin transactions in reverse order -- similar to disconnectblock()
        for (int i = block.vtx.size() - 1; i >= 0; i--) {
			const CTransaction &tx = block.vtx[i];
			if (tx.nVersion != SYSCOIN_TX_VERSION)
				continue;
			vector<vector<unsigned char> > vvch;
			int op, nOut;
			if(DecodeAliasTx(tx, op, nOut, vvch))
			{
				// remove the service before adding it again, because some of the checks in checkinputs relies on data already being there and just updating it, or not being there and adding it
				DisconnectAlias(pindex, tx, op, vvch);	
				CheckAliasInputs(tx, state, inputs, fBlock, fMiner, bCheckInputs, nHeight, true);		
			}
			if(DecodeOfferTx(tx, op, nOut, vvch))		
			{
				DisconnectOffer(pindex, tx, op, vvch);	
				CheckOfferInputs(tx, state, inputs, fBlock, fMiner, bCheckInputs, nHeight, true);
			}
			if(DecodeCertTx(tx, op, nOut, vvch))
			{
				DisconnectCertificate(pindex, tx, op, vvch);
				CheckCertInputs(tx, state, inputs, fBlock, fMiner, bCheckInputs, nHeight, true);
			}
			if(DecodeEscrowTx(tx, op, nOut, vvch))
			{
				DisconnectEscrow(pindex, tx, op, vvch);
				CheckEscrowInputs(tx, state, inputs, fBlock, fMiner, bCheckInputs, nHeight, true);
			}
			if(DecodeMessageTx(tx, op, nOut, vvch))
			{
				DisconnectMessage(pindex, tx, op, vvch);
				CheckMessageInputs(tx, state, inputs, fBlock, fMiner, bCheckInputs, nHeight, true);
			}
		}
		pindex = chainActive.Next(pindex);
	}
	
}

int IndexOfOfferOutput(const CTransaction& tx) {
	if (tx.nVersion != SYSCOIN_TX_VERSION)
		return -1;
	vector<vector<unsigned char> > vvch;
	int op, nOut;
	bool good = DecodeOfferTx(tx, op, nOut, vvch);
	if (!good)
		return -1;
	return nOut;
}

bool GetTxOfOffer(COfferDB& dbOffer, const vector<unsigned char> &vchOffer, 
				  COffer& txPos, CTransaction& tx) {
	vector<COffer> vtxPos;
	if (!pofferdb->ReadOffer(vchOffer, vtxPos) || vtxPos.empty())
		return false;
	txPos = vtxPos.back();
	int nHeight = txPos.nHeight;
	if (nHeight + GetOfferExpirationDepth()
			< chainActive.Tip()->nHeight) {
		string offer = stringFromVch(vchOffer);
		if(fDebug)
			LogPrintf("GetTxOfOffer(%s) : expired", offer.c_str());
		return false;
	}

	if (!GetSyscoinTransaction(nHeight, txPos.txHash, tx, Params().GetConsensus()))
		return false;

	return true;
}

bool GetTxOfOfferAccept(COfferDB& dbOffer, const vector<unsigned char> &vchOffer, const vector<unsigned char> &vchOfferAccept,
		COfferAccept &theOfferAccept, CTransaction& tx) {
	vector<COffer> vtxPos;
	if (!pofferdb->ReadOffer(vchOffer, vtxPos) || vtxPos.empty()) return false;
	theOfferAccept.SetNull();
	theOfferAccept.vchAcceptRand = vchOfferAccept;
	GetAcceptByHash(vtxPos, theOfferAccept);
	if(theOfferAccept.IsNull())
		return false;
	int nHeight = theOfferAccept.nHeight;
	if (nHeight + GetOfferExpirationDepth()
			< chainActive.Tip()->nHeight) {
		string offer = stringFromVch(vchOfferAccept);
		if(fDebug)
			LogPrintf("GetTxOfOfferAccept(%s) : expired", offer.c_str());
		return false;
	}

	if (!GetSyscoinTransaction(nHeight, theOfferAccept.txHash, tx, Params().GetConsensus()))
		return false;

	return true;
}
bool DecodeAndParseOfferTx(const CTransaction& tx, int& op, int& nOut,
		vector<vector<unsigned char> >& vvch)
{
	COffer offer(tx);
	bool decode = DecodeOfferTx(tx, op, nOut, vvch);
	bool parse = !offer.IsNull();
	return decode && parse;
}
bool DecodeOfferTx(const CTransaction& tx, int& op, int& nOut,
		vector<vector<unsigned char> >& vvch) {
	bool found = false;

	// Strict check - bug disallowed
	for (unsigned int i = 0; i < tx.vout.size(); i++) {
		const CTxOut& out = tx.vout[i];
		vector<vector<unsigned char> > vvchRead;
		if (DecodeOfferScript(out.scriptPubKey, op, vvch)) {
			nOut = i; found = true;
			break;
		}
	}
	if (!found) vvch.clear();
	return found && IsOfferOp(op);
}


bool DecodeOfferScript(const CScript& script, int& op,
		vector<vector<unsigned char> > &vvch, CScript::const_iterator& pc) {
	opcodetype opcode;
	vvch.clear();
	if (!script.GetOp(pc, opcode)) return false;
	if (opcode < OP_1 || opcode > OP_16) return false;
	op = CScript::DecodeOP_N(opcode);

	for (;;) {
		vector<unsigned char> vch;
		if (!script.GetOp(pc, opcode, vch))
			return false;
		if (opcode == OP_DROP || opcode == OP_2DROP || opcode == OP_NOP)
			break;
		if (!(opcode >= 0 && opcode <= OP_PUSHDATA4))
			return false;
		vvch.push_back(vch);
	}

	// move the pc to after any DROP or NOP
	while (opcode == OP_DROP || opcode == OP_2DROP || opcode == OP_NOP) {
		if (!script.GetOp(pc, opcode))
			break;
	}

	pc--;

	if ((op == OP_OFFER_ACTIVATE && vvch.size() == 1)
		|| (op == OP_OFFER_UPDATE && vvch.size() == 1)
		|| (op == OP_OFFER_ACCEPT && vvch.size() == 2)
		|| (op == OP_OFFER_REFUND && vvch.size() == 3))
		return true;
	return false;
}
bool DecodeOfferScript(const CScript& script, int& op,
		vector<vector<unsigned char> > &vvch) {
	CScript::const_iterator pc = script.begin();
	return DecodeOfferScript(script, op, vvch, pc);
}
CScript RemoveOfferScriptPrefix(const CScript& scriptIn) {
	int op;
	vector<vector<unsigned char> > vvch;
	CScript::const_iterator pc = scriptIn.begin();
	
	if (!DecodeOfferScript(scriptIn, op, vvch, pc))
	{
		throw runtime_error(
			"RemoveOfferScriptPrefix() : could not decode offer script");
	}

	return CScript(pc, scriptIn.end());
}

bool CheckOfferInputs(const CTransaction &tx,
		CValidationState &state, const CCoinsViewCache &inputs, bool fBlock, bool fMiner,
		bool fJustCheck, int nHeight, bool fRescan) {
	if (!tx.IsCoinBase()) {
		if (fDebug)
			LogPrintf("*** %d %d %s %s %s %s\n", nHeight,
				chainActive.Tip()->nHeight, tx.GetHash().ToString().c_str(),
				fBlock ? "BLOCK" : "", fMiner ? "MINER" : "",
				fJustCheck ? "JUSTCHECK" : "");
		bool fExternal = fInit || fRescan;
		bool foundOffer = false;
		bool foundCert = false;
		bool foundEscrow = false;
		const COutPoint *prevOutput = NULL;
		CCoins prevCoins;
		int prevOp, prevCertOp, prevEscrowOp;
		prevOp = prevCertOp = prevEscrowOp = 0;
		vector<vector<unsigned char> > vvchPrevArgs, vvchPrevCertArgs, vvchPrevEscrowArgs;
		// Strict check - bug disallowed
		for (unsigned int i = 0; i < tx.vin.size(); i++) {
			vector<vector<unsigned char> > vvch;
			int op;
			prevOutput = &tx.vin[i].prevout;
			if(!fExternal)
			{
				// ensure inputs are unspent when doing consensus check to add to block
				inputs.GetCoins(prevOutput->hash, prevCoins);
				IsSyscoinScript(prevCoins.vout[prevOutput->n].scriptPubKey, op, vvch);
			}
			else
				GetPreviousInput(prevOutput, op, vvch);

			if(foundEscrow && foundOffer && foundCert)
				break;

			if (!foundOffer && IsOfferOp(op)) {
				foundOffer = true; 
				prevOp = op;
				vvchPrevArgs = vvch;
			}
			else if (!foundCert && IsCertOp(op))
			{
				foundCert = true; 
				prevCertOp = op;
				vvchPrevCertArgs = vvch;
			}
			else if (!foundEscrow && IsEscrowOp(op))
			{
				foundEscrow = true; 
				prevEscrowOp = op;
				vvchPrevEscrowArgs = vvch;
			}
		}
		

		// Make sure offer outputs are not spent by a regular transaction, or the offer would be lost
		if (tx.nVersion != SYSCOIN_TX_VERSION) {
			if (foundOffer)
				return error(
						"CheckOfferInputs() : a non-syscoin transaction with a syscoin input");
			return true;
		}

		vector<vector<unsigned char> > vvchArgs;
		int op;
		int nOut;
		bool good = DecodeOfferTx(tx, op, nOut, vvchArgs);
		if (!good)
			return error("CheckOfferInputs() : could not decode offer tx");
		// unserialize offer from txn, check for valid
		COffer theOffer(tx);
		COfferAccept theOfferAccept;
		if (theOffer.IsNull())
			return error("CheckOfferInputs() : null offer");
		if(theOffer.sDescription.size() > MAX_VALUE_LENGTH)
		{
			return error("offer description too big");
		}
		if(theOffer.sTitle.size() > MAX_NAME_LENGTH)
		{
			return error("offer title too big");
		}
		if(theOffer.sCategory.size() > MAX_NAME_LENGTH)
		{
			return error("offer category too big");
		}
		if(theOffer.vchLinkOffer.size() > MAX_NAME_LENGTH)
		{
			return error("offer link guid too big");
		}
		if(theOffer.vchPubKey.size() > MAX_NAME_LENGTH)
		{
			return error("offer pub key too big");
		}
		if(theOffer.sCurrencyCode.size() > MAX_NAME_LENGTH)
		{
			return error("offer currency code too big");
		}
		if(theOffer.offerLinks.size() > 0)
		{
			return error("offer links are not allowed in tx data");
		}
		if(theOffer.linkWhitelist.entries.size() > 1)
		{
			return error("offer has too many whitelist entries, only one allowed per tx");
		}

		if (vvchArgs[0].size() > MAX_NAME_LENGTH)
			return error("offer hex guid too long");

		if(stringFromVch(theOffer.sCurrencyCode) != "BTC" && theOffer.bOnlyAcceptBTC)
		{
			return error("An offer that only accepts BTC must have BTC specified as its currency");
		}
		switch (op) {
		case OP_OFFER_ACTIVATE:
			if (foundOffer || (foundCert && !IsCertOp(prevCertOp)) )
				return error("CheckOfferInputs() :offeractivate previous op is invalid");		
			if (!theOffer.vchCert.empty() && !IsCertOp(prevCertOp))
				return error("CheckOfferInputs() : you must own a cert you wish to sell");			
			if (IsCertOp(prevCertOp) && !theOffer.vchCert.empty() && theOffer.vchCert != vvchPrevCertArgs[0])
				return error("CheckOfferInputs() : cert input and offer cert guid mismatch");
			// if we are selling a cert ensure it exists and pubkey's match (to ensure it doesnt get transferred prior to accepting by user)
			if(!theOffer.vchCert.empty())
			{
				CTransaction txCert;
				CCert theCert;
				// make sure this cert is still valid
				if (GetTxOfCert(*pcertdb, theOffer.vchCert, theCert, txCert))
				{
					if(theCert.vchPubKey != theOffer.vchPubKey)
						return error("CheckOfferInputs() OP_OFFER_ACTIVATE: cert and offer pubkey's must match, this cart may already be linked to another offer");
				}
				else
					return error("CheckOfferInputs() OP_OFFER_ACTIVATE: certificate does not exist or may be expired");
			}			
			if(!theOffer.vchLinkOffer.empty())
			{
				vector<COffer> myVtxPos;
				if (pofferdb->ExistsOffer(theOffer.vchLinkOffer)) {
					if (pofferdb->ReadOffer(theOffer.vchLinkOffer, myVtxPos))
					{
						COffer myParentOffer = myVtxPos.back();
						// ensure we can link against the offer if its in exclusive mode
						if(myParentOffer.linkWhitelist.bExclusiveResell)
						{
							if (!IsCertOp(prevCertOp) || theOffer.linkWhitelist.IsNull())
								return error("CheckOfferInputs() : you must own a cert you wish or link to");			
							if (IsCertOp(prevCertOp) && theOffer.linkWhitelist.entries[0].certLinkVchRand != vvchPrevCertArgs[0])
								return error("CheckOfferInputs() : cert input and offer whitelist guid mismatch");
						}
					}
					else
						return error("CheckOfferInputs() OP_OFFER_ACCEPT: invalid linked offer guid");	
				}
				else
					return error("CheckOfferInputs() OP_OFFER_ACCEPT: invalid linked offer guid");
			}
			break;
		case OP_OFFER_UPDATE:
			if (!IsOfferOp(prevOp) && !IsCertOp(prevCertOp) )
				return error("offerupdate previous op is invalid");			
			if (vvchPrevArgs[0] != vvchArgs[0])
				return error("CheckOfferInputs() : offerupdate offer mismatch");
			// if we are selling a cert ensure it exists and pubkey's match (to ensure it doesnt get transferred prior to accepting by user)
			if(!theOffer.vchCert.empty())
			{
				CTransaction txCert;
				CCert theCert;
				// make sure this cert is still valid
				if (GetTxOfCert(*pcertdb, theOffer.vchCert, theCert, txCert))
				{
					if(theCert.vchPubKey != theOffer.vchPubKey)
						return error("CheckOfferInputs() : offerupdate cert and offer pubkey mismatch");
				}
				else
					return error("CheckOfferInputs() OP_OFFER_UPDATE: certificate does not exist or may be expired");
			}
			break;
		case OP_OFFER_REFUND:
			if (prevOp != OP_OFFER_ACTIVATE && prevOp != OP_OFFER_UPDATE && prevOp != OP_OFFER_REFUND && prevOp != OP_OFFER_ACCEPT )
				return error("offerrefund previous op %s is invalid", offerFromOp(prevOp).c_str());		
			if(op == OP_OFFER_REFUND && vvchArgs[2] == OFFER_REFUND_COMPLETE && vvchPrevArgs[2] != OFFER_REFUND_PAYMENT_INPROGRESS)
				return error("offerrefund complete tx must be linked to an inprogress tx");
			
			if (vvchArgs[1].size() > MAX_NAME_LENGTH)
				return error("offerrefund tx with guid too big");
			if (vvchArgs[2].size() > MAX_ID_LENGTH)
				return error("offerrefund refund status too long");
			if (vvchPrevArgs[0] != vvchArgs[0])
				return error("CheckOfferInputs() : offerrefund offer mismatch");		
			// check for existence of offeraccept in txn offer obj
			theOfferAccept = theOffer.accept;
			if(theOfferAccept.vchAcceptRand != vvchArgs[1])
				return error("OP_OFFER_REFUND could not read accept from offer txn");
		
			break;
		case OP_OFFER_ACCEPT:
			if (foundOffer || (foundEscrow && !IsEscrowOp(prevEscrowOp)) || (foundCert && !IsCertOp(prevCertOp)))
				return error("CheckOfferInputs() : offeraccept cert/escrow input tx mismatch");
			if (IsCertOp(prevCertOp) && !theOffer.vchCert.empty() && theOffer.vchCert != vvchPrevCertArgs[0])
				return error("CheckOfferInputs() : cert input and offer cerp guid mismatch");
			if (vvchArgs[1].size() > MAX_NAME_LENGTH)
				return error("offeraccept tx with guid too big");
			// check for existence of offeraccept in txn offer obj
			theOfferAccept = theOffer.accept;
			if(theOfferAccept.IsNull())
				return error("OP_OFFER_ACCEPT null accept object");
			if(theOfferAccept.vchAcceptRand != vvchArgs[1])
				return error("OP_OFFER_ACCEPT could not read accept from offer txn");
			if (theOfferAccept.vchAcceptRand.size() > MAX_NAME_LENGTH)
				return error("OP_OFFER_ACCEPT offer accept hex guid too long");
			if (theOfferAccept.vchMessage.size() > MAX_ENCRYPTED_VALUE_LENGTH)
				return error("OP_OFFER_ACCEPT message field too big");
			if (theOfferAccept.vchRefundAddress.size() > MAX_NAME_LENGTH)
				return error("OP_OFFER_ACCEPT refund field too big");
			if (theOfferAccept.vchLinkOfferAccept.size() > MAX_NAME_LENGTH)
				return error("OP_OFFER_ACCEPT offer accept link field too big");
			if (theOfferAccept.vchLinkOffer.size() > MAX_NAME_LENGTH)
				return error("OP_OFFER_ACCEPT offer link field too big");
			if(IsEscrowOp(prevEscrowOp))
			{	
				vector<CEscrow> escrowVtxPos;
				if (pescrowdb->ExistsEscrow(vvchPrevEscrowArgs[0])) {
					if (pescrowdb->ReadEscrow(vvchPrevEscrowArgs[0], escrowVtxPos) && !escrowVtxPos.empty())
					{	
						// we want the initial funding escrow transaction height as when to calculate this offer accept price
						CEscrow fundingEscrow = escrowVtxPos.front();
						if(fundingEscrow.vchOffer != vvchArgs[0])
							return error("CheckOfferInputs() OP_OFFER_ACCEPT: escrow guid does not match the guid of the offer you are accepting");		
					}
					else
						return error("CheckOfferInputs() OP_OFFER_ACCEPT: invalid escrow guid");		
				}
				else
					return error("CheckOfferInputs() OP_OFFER_ACCEPT: invalid escrow guid");	
			}
			// trying to purchase a cert
			if(!theOffer.vchCert.empty())
			{
				CTransaction txCert;
				CCert theCert;
				// make sure this cert is still valid
				if (GetTxOfCert(*pcertdb, theOffer.vchCert, theCert, txCert))
				{
					// if we do an offeraccept based on an escrow release, it's assumed that the cert has already been transferred manually so buyer releases funds which can invalidate this accept
					// so in that case the escrow is attached to the accept and we skip this check
					// if the escrow is not attached means the buyer didnt use escrow, so ensure cert didn't get transferred since vendor created the offer in that case.
					if(theCert.vchPubKey != theOffer.vchPubKey && !IsEscrowOp(prevOp))
						return error("CheckOfferInputs() OP_OFFER_ACCEPT: cannot purchase this offer because the certificate has been transferred since it offer was created or it is linked to another offer");
					theOfferAccept.nQty = 1;
				}
				else
					return error("CheckOfferInputs() OP_OFFER_ACCEPT: certificate does not exist or may be expired");
			}
			break;

		default:
			return error( "CheckOfferInputs() : offer transaction has unknown op");
		}
		

		
		if (fBlock || (!fBlock && !fMiner && !fJustCheck)) {
			// save serialized offer for later use
			COffer serializedOffer = theOffer;
			COffer linkOffer;
			// load the offer data from the DB
			vector<COffer> vtxPos;
			if (pofferdb->ExistsOffer(vvchArgs[0]) && !fJustCheck) {
				if (!pofferdb->ReadOffer(vvchArgs[0], vtxPos))
					return error(
							"CheckOfferInputs() : failed to read from offer DB");
			}

			if (!fMiner && !fJustCheck && (chainActive.Tip()->nHeight != nHeight || fExternal)) {
				// get the latest offer from the db
				if(!vtxPos.empty())
					theOffer = vtxPos.back();				

				// If update, we make the serialized offer the master
				// but first we assign the offerLinks/whitelists from the DB since
				// they are not shipped in an update txn to keep size down
				if(op == OP_OFFER_UPDATE) {
					serializedOffer.offerLinks = theOffer.offerLinks;
					serializedOffer.accept.SetNull();
					theOffer = serializedOffer;
					if(!vtxPos.empty())
					{
						const COffer& dbOffer = vtxPos.back();
						// whitelist must be preserved in serialOffer and db offer must have the latest in the db for whitelists
						theOffer.linkWhitelist = dbOffer.linkWhitelist;
						// btc setting cannot change on update
						theOffer.bOnlyAcceptBTC = dbOffer.bOnlyAcceptBTC;
						// currency cannot change after creation
						theOffer.sCurrencyCode = dbOffer.sCurrencyCode;
						// some fields are only updated if they are not empty to limit txn size, rpc sends em as empty if we arent changing them
						if(serializedOffer.sCategory.empty())
							theOffer.sCategory = dbOffer.sCategory;
						if(serializedOffer.sTitle.empty())
							theOffer.sTitle = dbOffer.sTitle;
						if(serializedOffer.sDescription.empty())
							theOffer.sDescription = dbOffer.sDescription;
						if(serializedOffer.vchPubKey.empty())
							theOffer.vchPubKey = dbOffer.vchPubKey;
						if(serializedOffer.vchLinkOffer.empty())
							theOffer.vchLinkOffer = dbOffer.vchLinkOffer;
					}
					if(!theOffer.vchCert.empty())						
						theOffer.nQty = 1;
					
				}
				else if(op == OP_OFFER_ACTIVATE)
				{
					if(!theOffer.vchCert.empty())
						theOffer.nQty = 1;
						
					// if this is a linked offer activate, then add it to the parent offerLinks list
					if(!theOffer.vchLinkOffer.empty())
					{
						vector<COffer> myVtxPos;
						if (pofferdb->ExistsOffer(theOffer.vchLinkOffer)) {
							if (pofferdb->ReadOffer(theOffer.vchLinkOffer, myVtxPos))
							{
								COffer myParentOffer = myVtxPos.back();
								// if creating a linked offer we set some mandatory fields to the parent
								theOffer.nQty = myParentOffer.nQty;
								theOffer.vchPubKey = myParentOffer.vchPubKey;
								theOffer.sCategory = myParentOffer.sCategory;
								theOffer.sTitle = myParentOffer.sTitle;
								theOffer.linkWhitelist.bExclusiveResell = true;
								theOffer.sCurrencyCode = myParentOffer.sCurrencyCode;

								myParentOffer.offerLinks.push_back(vvchArgs[0]);							
								myParentOffer.PutToOfferList(myVtxPos);
								{
								TRY_LOCK(cs_main, cs_trymain);
								// write parent offer
								if (!pofferdb->WriteOffer(theOffer.vchLinkOffer, myVtxPos))
									return error( "CheckOfferInputs() : failed to write to offer link to DB");
								}
							}
						}
						
					}
				}
				else if(op == OP_OFFER_REFUND)
				{
					vector<unsigned char> vchOfferAccept = vvchArgs[1];
					theOfferAccept.vchAcceptRand = vchOfferAccept;
					if(!GetAcceptByHash(vtxPos, theOfferAccept))
					{
						LogPrintf("CheckOfferInputs()- OP_OFFER_REFUND: could not read accept from db offer txn");
						return true;
					}
					if(!fExternal &&  pwalletMain && vvchArgs[2] == OFFER_REFUND_PAYMENT_INPROGRESS){
						string strError = makeOfferRefundTX(tx, vvchArgs[0], vchOfferAccept, OFFER_REFUND_COMPLETE);
						if (strError != "" && fDebug)							
							LogPrintf("CheckOfferInputs() - OFFER_REFUND_COMPLETE %s\n", strError.c_str());
						// if this accept was done via offer linking (makeOfferLinkAcceptTX) then walk back up and refund
						// special case for tmp it will get overwritten by theOfferAccept.vchLinkOfferAccept tx inside makeOfferRefundTX (used for input transaction to this refund tx)
						CTransaction tmp;
						strError = makeOfferRefundTX(tmp, theOfferAccept.vchLinkOffer, theOfferAccept.vchLinkOfferAccept, OFFER_REFUND_PAYMENT_INPROGRESS);
						if (strError != "" && fDebug)							
							LogPrintf("CheckOfferInputs() - OFFER_REFUND_PAYMENT_INPROGRESS %s\n", strError.c_str());			
					}
					else if(vvchArgs[2] == OFFER_REFUND_COMPLETE){
						theOfferAccept.bRefunded = true;
					}
					theOfferAccept.nHeight = nHeight;
					theOfferAccept.txHash = tx.GetHash();
					theOffer.accept = theOfferAccept;	
					
				}
				else if (op == OP_OFFER_ACCEPT) {	
					if(stringFromVch(theOffer.sCurrencyCode) != "BTC" && !theOfferAccept.txBTCId.IsNull())
					{
						LogPrintf("CheckOfferInputs() OP_OFFER_ACCEPT: can't accept an offer for BTC that isn't specified in BTC by owner");					
						theOfferAccept.txBTCId.SetNull();
					}
					COffer myOffer,linkOffer;
					CTransaction offerTx, linkedTx;			
					// find the payment from the tx outputs (make sure right amount of coins were paid for this offer accept), the payment amount found has to be exact	
					uint64_t heightToCheckAgainst = theOfferAccept.nHeight;
					COfferLinkWhitelistEntry entry;
					if(IsCertOp(prevCertOp))
						theOffer.linkWhitelist.GetLinkEntryByHash(vvchPrevCertArgs[0], entry);	
		
					// if this accept was done via an escrow release, we get the height from escrow and use that to lookup the price at the time
					if(IsEscrowOp(prevEscrowOp))
					{	
						vector<CEscrow> escrowVtxPos;
						if (pescrowdb->ExistsEscrow(vvchPrevEscrowArgs[0])) {
							if (pescrowdb->ReadEscrow(vvchPrevEscrowArgs[0], escrowVtxPos) && !escrowVtxPos.empty())
							{	
								// we want the initial funding escrow transaction height as when to calculate this offer accept price
								CEscrow fundingEscrow = escrowVtxPos.front();
								heightToCheckAgainst = fundingEscrow.nHeight;
							}
						}
					}
					// check that user pays enough in syscoin if the currency of the offer is not bitcoin or there is no bitcoin transaction ID associated with this accept
					if(stringFromVch(theOffer.sCurrencyCode) != "BTC" || theOfferAccept.txBTCId.IsNull())
					{
						COffer myPriceOffer;
						myPriceOffer.nHeight = heightToCheckAgainst;
						myPriceOffer.GetOfferFromList(vtxPos);
						int precision = 2;
						// lookup the price of the offer in syscoin based on pegged alias at the block # when accept/escrow was made
						CAmount nPrice = convertCurrencyCodeToSyscoin(theOffer.sCurrencyCode, myPriceOffer.GetPrice(entry), heightToCheckAgainst, precision)*theOfferAccept.nQty;
						if(tx.vout[nOut].nValue != nPrice)
							return error("CheckOfferInputs() OP_OFFER_ACCEPT: this offer accept does not pay enough according to the offer price %ld, currency %s, value found %ld\n", nPrice, stringFromVch(theOffer.sCurrencyCode).c_str(), tx.vout[nOut].nValue);											
						theOfferAccept.bPaid = true;

					}
								
					if (!GetTxOfOffer(*pofferdb, vvchArgs[0], myOffer, offerTx))
						return error("CheckOfferInputs() OP_OFFER_ACCEPT: could not find an offer with this name");


					if(!myOffer.vchLinkOffer.empty())
					{
						if(!GetTxOfOffer(*pofferdb, myOffer.vchLinkOffer, linkOffer, linkedTx))
							linkOffer.SetNull();
					}

					// 2 step refund: send an offer accept with nRefunded property set to inprogress and then send another with complete later
					// first step is to send inprogress so that next block we can send a complete (also sends coins during second step to original acceptor)
					// this is to ensure that the coins sent during accept are available to spend to refund to avoid having to hold double the balance of an accept amount
					// in order to refund.
					if(theOfferAccept.nQty <= 0 || (theOfferAccept.nQty > theOffer.nQty || (!linkOffer.IsNull() && theOfferAccept.nQty > linkOffer.nQty))) {
						if(!fExternal)
						{
							string strError = makeOfferRefundTX(offerTx, vvchArgs[0], vvchArgs[1], OFFER_REFUND_PAYMENT_INPROGRESS);
							if (strError != "" && fDebug)
								LogPrintf("CheckOfferInputs() - OP_OFFER_ACCEPT %s\n", strError.c_str());
						}
						return error("txn %s rejected because desired qty %u is more than available qty %u\n", tx.GetHash().GetHex().c_str(), theOfferAccept.nQty, theOffer.nQty);
					}

					// only if we are the root offer owner do we even consider xfering a cert					
					// purchased a cert so xfer it
					if(!fExternal && pwalletMain && IsSyscoinTxMine(offerTx) && !theOffer.vchCert.empty() && theOffer.vchLinkOffer.empty())
					{
						string strError = makeTransferCertTX(theOffer, theOfferAccept);
						if(strError != "")
						{
							if(fDebug)
							{
								LogPrintf("CheckOfferInputs() - OP_OFFER_ACCEPT - makeTransferCertTX %s\n", strError.c_str());
							}
						}
					}
				
					if(theOffer.vchLinkOffer.empty())
					{
						theOffer.nQty -= theOfferAccept.nQty;
						// go through the linked offers, if any, and update the linked offer qty based on the this qty
						for(unsigned int i=0;i<theOffer.offerLinks.size();i++) {
							vector<COffer> myVtxPos;
							if (pofferdb->ExistsOffer(theOffer.offerLinks[i])) {
								if (pofferdb->ReadOffer(theOffer.offerLinks[i], myVtxPos))
								{
									COffer myLinkOffer = myVtxPos.back();
									myLinkOffer.nQty = theOffer.nQty;	
									myLinkOffer.PutToOfferList(myVtxPos);
									{
									TRY_LOCK(cs_main, cs_trymain);
									// write offer
									if (!pofferdb->WriteOffer(theOffer.offerLinks[i], myVtxPos))
										return error( "CheckOfferInputs() : failed to write to offer link to DB");
									}
								}
							}
						}
					}
					if (!fExternal && pwalletMain && !linkOffer.IsNull() && IsSyscoinTxMine(offerTx))
					{	
						// vchPubKey is for when transfering cert after an offer accept, the pubkey is the transfer-to address and encryption key for cert data
						// myOffer.vchLinkOffer is the linked offer guid
						// vvchArgs[1] is this offer accept rand used to walk back up and refund offers in the linked chain
						// we are now accepting the linked	 offer, up the link offer stack.

						string strError = makeOfferLinkAcceptTX(theOfferAccept, myOffer.vchLinkOffer, vvchArgs[1]);
						if(strError != "")
						{
							if(fDebug)
							{
								LogPrintf("CheckOfferInputs() - OP_OFFER_ACCEPT - makeOfferLinkAcceptTX %s\n", strError.c_str());
							}
							// if there is a problem refund this accept
							strError = makeOfferRefundTX(offerTx, vvchArgs[0], vvchArgs[1], OFFER_REFUND_PAYMENT_INPROGRESS);
							if (strError != "" && fDebug)
								LogPrintf("CheckOfferInputs() - OP_OFFER_ACCEPT - makeOfferLinkAcceptTX(makeOfferRefundTX) %s\n", strError.c_str());

						}
					}
					theOfferAccept.nHeight = nHeight;
					theOfferAccept.vchAcceptRand = vvchArgs[1];
					theOfferAccept.txHash = tx.GetHash();
					theOffer.accept = theOfferAccept;
				}
				
				if(op == OP_OFFER_ACTIVATE || op == OP_OFFER_UPDATE) {
					if(op == OP_OFFER_UPDATE)
					{
						// if the txn whitelist entry exists (meaning we want to remove or add)
						if(serializedOffer.linkWhitelist.entries.size() == 1)
						{
							COfferLinkWhitelistEntry entry;
							// special case we use to remove all entries
							if(serializedOffer.linkWhitelist.entries[0].nDiscountPct == 127)
							{
								theOffer.linkWhitelist.SetNull();
							}
							// the stored offer has this entry meaning we want to remove this entry
							else if(theOffer.linkWhitelist.GetLinkEntryByHash(serializedOffer.linkWhitelist.entries[0].certLinkVchRand, entry))
							{
								theOffer.linkWhitelist.RemoveWhitelistEntry(serializedOffer.linkWhitelist.entries[0].certLinkVchRand);
							}
							// we want to add it to the whitelist
							else
							{
								if(!serializedOffer.linkWhitelist.entries[0].certLinkVchRand.empty() && serializedOffer.linkWhitelist.entries[0].nDiscountPct <= 99)
									theOffer.linkWhitelist.PutWhitelistEntry(serializedOffer.linkWhitelist.entries[0]);
							}
						}
						// if this offer is linked to a parent update it with parent information
						if(!theOffer.vchLinkOffer.empty())
						{
							vector<COffer> myVtxPos;
							if (pofferdb->ExistsOffer(theOffer.vchLinkOffer)) {
								if (pofferdb->ReadOffer(theOffer.vchLinkOffer, myVtxPos))
								{
									COffer myLinkOffer = myVtxPos.back();
									theOffer.nQty = myLinkOffer.nQty;	
									theOffer.nHeight = myLinkOffer.nHeight;
									theOffer.SetPrice(myLinkOffer.nPrice);
									
								}
							}
								
						}
						else
						{
							// go through the linked offers, if any, and update the linked offer info based on the this info
							for(unsigned int i=0;i<theOffer.offerLinks.size();i++) {
								vector<COffer> myVtxPos;
								if (pofferdb->ExistsOffer(theOffer.offerLinks[i])) {
									if (pofferdb->ReadOffer(theOffer.offerLinks[i], myVtxPos))
									{
										COffer myLinkOffer = myVtxPos.back();
										myLinkOffer.nQty = theOffer.nQty;	
										myLinkOffer.SetPrice(theOffer.nPrice);
										myLinkOffer.PutToOfferList(myVtxPos);
										{
										TRY_LOCK(cs_main, cs_trymain);
										// write offer
										if (!pofferdb->WriteOffer(theOffer.offerLinks[i], myVtxPos))
											return error( "CheckOfferInputs() : failed to write to offer link to DB");
										}
									}
								}
							}
							
						}
					}
				}
				theOffer.nHeight = nHeight;
				theOffer.txHash = tx.GetHash();
				theOffer.PutToOfferList(vtxPos);
				{
				TRY_LOCK(cs_main, cs_trymain);
				// write offer
				if (!pofferdb->WriteOffer(vvchArgs[0], vtxPos))
					return error( "CheckOfferInputs() : failed to write to offer DB");

               				
				// debug
				if (fDebug)
					LogPrintf( "CONNECTED OFFER: op=%s offer=%s title=%s qty=%u hash=%s height=%d\n",
						offerFromOp(op).c_str(),
						stringFromVch(vvchArgs[0]).c_str(),
						stringFromVch(theOffer.sTitle).c_str(),
						theOffer.nQty,
						tx.GetHash().ToString().c_str(), 
						nHeight);
				}
			}
		}
	}
	return true;
}


void rescanforsyscoinservices(CBlockIndex *pindexRescan) {
   LogPrintf("Scanning blockchain for syscoin services to create fast index...\n");
   ReconstructSyscoinServicesIndex(pindexRescan);
}



UniValue offernew(const UniValue& params, bool fHelp) {
	if (fHelp || params.size() < 7 || params.size() > 11)
		throw runtime_error(
		"offernew <alias> <category> <title> <quantity> <price> <description> <currency> [private=0] [cert. guid] [exclusive resell=1] [accept btc only=0]\n"
						"<alias> An alias you own\n"
						"<category> category, 255 chars max.\n"
						"<title> title, 255 chars max.\n"
						"<quantity> quantity, > 0\n"
						"<price> price in <currency>, > 0\n"
						"<description> description, 1 KB max.\n"
						"<currency> The currency code that you want your offer to be in ie: USD.\n"
						"<private> Is this a private offer. Default 0 for false.\n"
						"<cert. guid> Set this to the guid of a certificate you wish to sell\n"
						"<exclusive resell> set to 1 if you only want those who control the whitelist certificates to be able to resell this offer via offerlink. Defaults to 1.\n"
						"<accept btc only> set to 1 if you only want accept Bitcoins for payment and your currency is set to BTC, note you cannot resell or sell a cert in this mode. Defaults to 0.\n"
						+ HelpRequiringPassphrase());
	// gather inputs
	string baSig;
	float nPrice;
	bool bPrivate = false;
	bool bExclusiveResell = true;
	vector<unsigned char> vchAlias = vchFromValue(params[0]);
	CSyscoinAddress aliasAddress = CSyscoinAddress(stringFromVch(vchAlias));
	if (!aliasAddress.IsValid())
		throw runtime_error("Invalid syscoin address");
	if (!aliasAddress.isAlias)
		throw runtime_error("Arbiter must be a valid alias");

	// check for alias existence in DB
	vector<CAliasIndex> vtxPos;
	if (!paliasdb->ReadAlias(vchFromString(aliasAddress.aliasName), vtxPos))
		throw runtime_error("failed to read alias from alias DB");
	if (vtxPos.size() < 1)
		throw runtime_error("no result returned");
	CAliasIndex alias = vtxPos.back();
	vector<unsigned char> vchCat = vchFromValue(params[1]);
	vector<unsigned char> vchTitle = vchFromValue(params[2]);
	vector<unsigned char> vchCurrency = vchFromValue(params[6]);
	vector<unsigned char> vchDesc;
	vector<unsigned char> vchCert;
	bool bOnlyAcceptBTC = false;
	unsigned int nQty;
	if(atof(params[3].get_str().c_str()) < 0)
		throw runtime_error("invalid quantity value, must be greator than 0");
	try {
		nQty = boost::lexical_cast<unsigned int>(params[3].get_str());
	} catch (std::exception &e) {
		throw runtime_error("invalid quantity value, must be less than 4294967296");
	}
	nPrice = atof(params[4].get_str().c_str());
	if(nPrice <= 0)
	{
		throw runtime_error("offer price must be greater than 0!");
	}
	vchDesc = vchFromValue(params[5]);
	if(vchCat.size() < 1)
        throw runtime_error("offer category cannot be empty!");
	if(vchTitle.size() < 1)
        throw runtime_error("offer title cannot be empty!");
	if(vchCat.size() > MAX_NAME_LENGTH)
        throw runtime_error("offer category cannot exceed 255 bytes!");
	if(vchTitle.size() > MAX_NAME_LENGTH)
        throw runtime_error("offer title cannot exceed 255 bytes!");
    // 1Kbyte offer desc. maxlen
	if (vchDesc.size() > MAX_VALUE_LENGTH)
		throw runtime_error("offer description cannot exceed 1023 bytes!");
	CScript scriptPubKeyOrig, scriptPubKeyCertOrig;
	CScript scriptPubKey, scriptPubKeyCert;
	const CWalletTx *wtxCertIn = NULL;
	CCert theCert;
	if(params.size() >= 8)
	{
		bPrivate = atoi(params[7].get_str().c_str()) == 1? true: false;
	}
	if(params.size() >= 9)
	{
		
		vchCert = vchFromValue(params[8]);
		CTransaction txCert;
		
		
		// make sure this cert is still valid
		if (GetTxOfCert(*pcertdb, vchCert, theCert, txCert))
		{
			wtxCertIn = pwalletMain->GetWalletTx(txCert.GetHash());
			// make sure its in your wallet (you control this cert)		
			if (IsSyscoinTxMine(txCert) && wtxCertIn != NULL) 
			{
				std::vector<unsigned char> vchCertKeyByte;
				boost::algorithm::unhex(theCert.vchPubKey.begin(), theCert.vchPubKey.end(), std::back_inserter(vchCertKeyByte));
				CPubKey currentCertKey(vchCertKeyByte);
				scriptPubKeyCertOrig = GetScriptForDestination(currentCertKey.GetID());
			}
			else
				throw runtime_error("You must own this cert to sell it");
			scriptPubKeyCert << CScript::EncodeOP_N(OP_CERT_UPDATE) << vchCert << OP_2DROP;
			scriptPubKeyCert += scriptPubKeyCertOrig;		
		}
		else
			vchCert.clear();
	}

	if(params.size() >= 10)
	{
		bExclusiveResell = atoi(params[9].get_str().c_str()) == 1? true: false;
	}
	if(params.size() >= 11)
	{
		bOnlyAcceptBTC = atoi(params[10].get_str().c_str()) == 1? true: false;
		if(bOnlyAcceptBTC && !vchCert.empty())
			throw runtime_error("Cannot sell a certificate accepting only Bitcoins");
		if(bOnlyAcceptBTC && stringFromVch(vchCurrency) != "BTC")
			throw runtime_error("Can only accept Bitcoins for offer's that set their currency to BTC");

	}	
	CAmount nRate;
	vector<string> rateList;
	int precision;
	if(getCurrencyToSYSFromAlias(vchCurrency, nRate, chainActive.Tip()->nHeight, rateList,precision) != "")
	{
		string err = strprintf("Could not find currency %s in the SYS_RATES alias!\n", stringFromVch(vchCurrency));
		throw runtime_error(err.c_str());
	}
	double minPrice = pow(10.0,-precision);
	double price = nPrice;
	if(price < minPrice)
		price = minPrice; 
	// this is a syscoin transaction
	CWalletTx wtx;

	// generate rand identifier
	int64_t rand = GetRand(std::numeric_limits<int64_t>::max());
	vector<unsigned char> vchRand = CScriptNum(rand).getvch();
	vector<unsigned char> vchOffer = vchFromString(HexStr(vchRand));

	EnsureWalletIsUnlocked();


  	CPubKey newDefaultKey;
	// unserialize offer UniValue from txn, serialize back
	// build offer UniValue
	COffer newOffer;
	if(wtxCertIn == NULL)
		newOffer.vchPubKey = alias.vchPubKey;
	else
		newOffer.vchPubKey = theCert.vchPubKey;
	newOffer.sCategory = vchCat;
	newOffer.sTitle = vchTitle;
	newOffer.sDescription = vchDesc;
	newOffer.nQty = nQty;
	newOffer.nHeight = chainActive.Tip()->nHeight;
	newOffer.SetPrice(price);
	newOffer.vchCert = vchCert;
	newOffer.linkWhitelist.bExclusiveResell = bExclusiveResell;
	newOffer.sCurrencyCode = vchCurrency;
	newOffer.bPrivate = bPrivate;
	newOffer.bOnlyAcceptBTC = bOnlyAcceptBTC;
	
	scriptPubKeyOrig= GetScriptForDestination(aliasAddress.Get());
	scriptPubKey << CScript::EncodeOP_N(OP_OFFER_ACTIVATE) << vchOffer << OP_2DROP;
	scriptPubKey += scriptPubKeyOrig;

	vector<CRecipient> vecSend;
	CRecipient recipient;
	CreateRecipient(scriptPubKey, recipient);
	vecSend.push_back(recipient);
	CRecipient certRecipient;
	CreateRecipient(scriptPubKeyCert, certRecipient);
	// if we use a cert as input to this offer tx, we need another utxo for further cert transactions on this cert, so we create one here
	if(wtxCertIn != NULL)
		vecSend.push_back(certRecipient);
	const vector<unsigned char> &data = newOffer.Serialize();
	CScript scriptData;
	scriptData << OP_RETURN << data;
	CRecipient fee;
	CreateFeeRecipient(scriptData, data, fee);
	vecSend.push_back(fee);
	const CWalletTx * wtxInOffer=NULL;
	const CWalletTx * wtxInAlias=NULL;
	const CWalletTx * wtxInEscrow=NULL;
	SendMoneySyscoin(vecSend, recipient.nAmount+fee.nAmount, false, wtx, wtxInOffer, wtxCertIn, wtxInAlias, wtxInEscrow);
	UniValue res(UniValue::VARR);
	res.push_back(wtx.GetHash().GetHex());
	res.push_back(HexStr(vchRand));
	return res;
}

UniValue offerlink(const UniValue& params, bool fHelp) {
	if (fHelp || params.size() < 2 || params.size() > 3)
		throw runtime_error(
		"offerlink <guid> <commission> [description]\n"
						"<alias> An alias you own\n"
						"<guid> offer guid that you are linking to\n"
						"<commission> percentage of profit desired over original offer price, > 0, ie: 5 for 5%\n"
						"<description> description, 1 KB max. Defaults to original description. Leave as '' to use default.\n"
						+ HelpRequiringPassphrase());
	// gather inputs
	string baSig;
	COfferLinkWhitelistEntry whiteListEntry;

	vector<unsigned char> vchLinkOffer = vchFromValue(params[0]);
	vector<unsigned char> vchDesc;
	// look for a transaction with this key
	CTransaction tx;
	COffer linkOffer;
	if (!GetTxOfOffer(*pofferdb, vchLinkOffer, linkOffer, tx) || vchLinkOffer.empty())
		throw runtime_error("could not find an offer with this name");

	if(!linkOffer.vchLinkOffer.empty())
	{
		throw runtime_error("cannot link to an offer that is already linked to another offer");
	}

	int commissionInteger = atoi(params[1].get_str().c_str());
	if(commissionInteger < 0 || commissionInteger > 255)
	{
		throw runtime_error("commission must positive and less than 256!");
	}
	
	if(params.size() >= 3)
	{

		vchDesc = vchFromValue(params[2]);
		if(vchDesc.size() > 0)
		{
			// 1kbyte offer desc. maxlen
			if (vchDesc.size() > MAX_VALUE_LENGTH)
				throw runtime_error("offer description cannot exceed 1023 bytes!");
		}
		else
		{
			vchDesc = linkOffer.sDescription;
		}
	}
	else
	{
		vchDesc = linkOffer.sDescription;
	}	


	COfferLinkWhitelistEntry foundEntry;
	CScript scriptPubKeyOrig, scriptPubKeyCertOrig;
	CScript scriptPubKey, scriptPubKeyCert;
	const CWalletTx *wtxCertIn = NULL;
	if(linkOffer.linkWhitelist.bExclusiveResell)
	{
		// go through the whitelist and see if you own any of the certs to apply to this offer for a discount
		for(unsigned int i=0;i<linkOffer.linkWhitelist.entries.size();i++) {
			CTransaction txCert;
			const CWalletTx *wtxCertIn;
			CCert theCert;
			COfferLinkWhitelistEntry& entry = linkOffer.linkWhitelist.entries[i];
			// make sure this cert is still valid
			if (GetTxOfCert(*pcertdb, entry.certLinkVchRand, theCert, txCert))
			{
				wtxCertIn = pwalletMain->GetWalletTx(txCert.GetHash());
				// make sure its in your wallet (you control this cert)		
				if (IsSyscoinTxMine(txCert) && wtxCertIn != NULL) 
				{
					foundEntry = entry;
					std::vector<unsigned char> vchCertKeyByte;
					boost::algorithm::unhex(theCert.vchPubKey.begin(), theCert.vchPubKey.end(), std::back_inserter(vchCertKeyByte));
					CPubKey currentCertKey(vchCertKeyByte);
					scriptPubKeyCertOrig = GetScriptForDestination(currentCertKey.GetID());
				}
				else
					wtxCertIn = NULL;
			}
		}

		// if the whitelist exclusive mode is on and you dont have a cert in the whitelist, you cannot link to this offer
		if(foundEntry.IsNull())
		{
			throw runtime_error("cannot link to this offer because you don't own a cert from its whitelist (the offer is in exclusive mode)");
		}
		scriptPubKeyCert << CScript::EncodeOP_N(OP_CERT_UPDATE) << foundEntry.certLinkVchRand << OP_2DROP;
		scriptPubKeyCert += scriptPubKeyCertOrig;
	}
	if(linkOffer.bOnlyAcceptBTC)
	{
		throw runtime_error("Cannot link to an offer that only accepts Bitcoins as payment");
	}
	// this is a syscoin transaction
	CWalletTx wtx;


	// generate rand identifier
	int64_t rand = GetRand(std::numeric_limits<int64_t>::max());
	vector<unsigned char> vchRand = CScriptNum(rand).getvch();
	vector<unsigned char> vchOffer = vchFromString(HexStr(vchRand));
	int precision = 2;
	// get precision
	convertCurrencyCodeToSyscoin(linkOffer.sCurrencyCode, linkOffer.GetPrice(), chainActive.Tip()->nHeight, precision);
	double minPrice = pow(10.0,-precision);
	double price = linkOffer.GetPrice();
	if(price < minPrice)
		price = minPrice;

	EnsureWalletIsUnlocked();
	
	// unserialize offer from txn, serialize back
	// build offer
	COffer newOffer;

	newOffer.sDescription = vchDesc;
	newOffer.SetPrice(price);
	newOffer.nCommission = commissionInteger;
	newOffer.vchLinkOffer = vchLinkOffer;
	newOffer.linkWhitelist.PutWhitelistEntry(foundEntry);
	newOffer.nHeight = chainActive.Tip()->nHeight;
	
	//create offeractivate txn keys
	CPubKey newDefaultKey;
	pwalletMain->GetKeyFromPool(newDefaultKey);
	
	scriptPubKeyOrig= GetScriptForDestination(newDefaultKey.GetID());
	scriptPubKey << CScript::EncodeOP_N(OP_OFFER_ACTIVATE) << vchOffer << OP_2DROP;
	scriptPubKey += scriptPubKeyOrig;


	string strError;

	vector<CRecipient> vecSend;
	CRecipient recipient;
	CreateRecipient(scriptPubKey, recipient);
	vecSend.push_back(recipient);
	CRecipient certRecipient;
	CreateRecipient(scriptPubKeyCert, certRecipient);
	// if we use a cert as input to this offer tx, we need another utxo for further cert transactions on this cert, so we create one here
	if(wtxCertIn != NULL)
		vecSend.push_back(certRecipient);

	const vector<unsigned char> &data = newOffer.Serialize();
	CScript scriptData;
	scriptData << OP_RETURN << data;
	CRecipient fee;
	CreateFeeRecipient(scriptData, data, fee);
	vecSend.push_back(fee);
	const CWalletTx * wtxInOffer=NULL;
	const CWalletTx * wtxInAlias=NULL;
	const CWalletTx * wtxInEscrow=NULL;
	SendMoneySyscoin(vecSend, recipient.nAmount+fee.nAmount, false, wtx, wtxInOffer, wtxCertIn, wtxInAlias, wtxInEscrow);

	UniValue res(UniValue::VARR);
	res.push_back(wtx.GetHash().GetHex());
	res.push_back(HexStr(vchRand));
	return res;
}
UniValue offeraddwhitelist(const UniValue& params, bool fHelp) {
	if (fHelp || params.size() < 2 || params.size() > 3)
		throw runtime_error(
		"offeraddwhitelist <offer guid> <cert guid> [discount percentage]\n"
		"Add to the whitelist of your offer(controls who can resell).\n"
						"<offer guid> offer guid that you are adding to\n"
						"<cert guid> cert guid representing a certificate that you control (transfer it to reseller after)\n"
						"<discount percentage> percentage of discount given to reseller for this offer. Negative discount adds on top of offer price, acts as an extra commission. -99 to 99.\n"						
						+ HelpRequiringPassphrase());

	// gather & validate inputs
	vector<unsigned char> vchOffer = vchFromValue(params[0]);
	vector<unsigned char> vchCert =  vchFromValue(params[1]);
	int nDiscountPctInteger = 0;
	
	if(params.size() >= 3)
		nDiscountPctInteger = atoi(params[2].get_str().c_str());

	if(nDiscountPctInteger < -99 || nDiscountPctInteger > 99)
		throw runtime_error("Invalid discount amount");
	CTransaction txCert;
	CCert theCert;
	CWalletTx wtx;
	const CWalletTx* wtxIn;
	const CWalletTx *wtxCertIn = NULL;
	if (!GetTxOfCert(*pcertdb, vchCert, theCert, txCert))
		throw runtime_error("could not find a certificate with this key");

	// this is a syscoin txn
	CScript scriptPubKeyOrig, scriptPubKeyCertOrig;
	// create OFFERUPDATE/CERTUPDATE txn keys
	CScript scriptPubKey, scriptPubKeyCert;
	// check to see if certificate in wallet
	wtxCertIn = pwalletMain->GetWalletTx(txCert.GetHash());
	if (!IsSyscoinTxMine(txCert) || wtxCertIn == NULL) 
		throw runtime_error("this certificate is not yours");

	std::vector<unsigned char> vchCertKeyByte;
	boost::algorithm::unhex(theCert.vchPubKey.begin(), theCert.vchPubKey.end(), std::back_inserter(vchCertKeyByte));
	CPubKey currentCertKey(vchCertKeyByte);
	scriptPubKeyCertOrig = GetScriptForDestination(currentCertKey.GetID());
	scriptPubKeyCert << CScript::EncodeOP_N(OP_CERT_UPDATE) << vchCert << OP_2DROP;
	scriptPubKeyCert += scriptPubKeyCertOrig;


	EnsureWalletIsUnlocked();

	// look for a transaction with this key
	CTransaction tx;
	COffer theOffer;
	if (!GetTxOfOffer(*pofferdb, vchOffer, theOffer, tx))
		throw runtime_error("could not find an offer with this name");

	std::vector<unsigned char> vchKeyByte;
	boost::algorithm::unhex(theOffer.vchPubKey.begin(), theOffer.vchPubKey.end(), std::back_inserter(vchKeyByte));
	CPubKey currentKey(vchKeyByte);
	scriptPubKeyOrig = GetScriptForDestination(currentKey.GetID());

	wtxIn = pwalletMain->GetWalletTx(tx.GetHash());
	if (wtxIn == NULL)
		throw runtime_error("this offer is not in your wallet");
	// check for existing pending offers
	if (ExistsInMempool(vchOffer, OP_OFFER_REFUND) || ExistsInMempool(vchOffer, OP_OFFER_ACTIVATE) || ExistsInMempool(vchOffer, OP_OFFER_UPDATE)) {
		throw runtime_error("there are pending operations or refunds on that offer");
	}
	// unserialize offer from txn
	if(!theOffer.UnserializeFromTx(tx))
		throw runtime_error("cannot unserialize offer from txn");

	// get the offer from DB
	vector<COffer> vtxPos;
	if (!pofferdb->ReadOffer(vchOffer, vtxPos) || vtxPos.empty())
		throw runtime_error("could not read offer from DB");

	theOffer = vtxPos.back();
	theOffer.nHeight = chainActive.Tip()->nHeight;
	for(unsigned int i=0;i<theOffer.linkWhitelist.entries.size();i++) {
		COfferLinkWhitelistEntry& entry = theOffer.linkWhitelist.entries[i];
		// make sure this cert doesn't already exist
		if (entry.certLinkVchRand == vchCert)
		{
			throw runtime_error("This cert is already added to your whitelist");
		}

	}
	scriptPubKey << CScript::EncodeOP_N(OP_OFFER_UPDATE) << vchOffer << OP_2DROP;
	scriptPubKey += scriptPubKeyOrig;

	COfferLinkWhitelistEntry entry;
	entry.certLinkVchRand = vchCert;
	entry.nDiscountPct = nDiscountPctInteger;
	theOffer.ClearOffer();
	theOffer.linkWhitelist.PutWhitelistEntry(entry);

	vector<CRecipient> vecSend;
	CRecipient recipient;
	CreateRecipient(scriptPubKey, recipient);
	vecSend.push_back(recipient);
	CRecipient certRecipient;
	CreateRecipient(scriptPubKeyCert, certRecipient);
	// if we use a cert as input to this offer tx, we need another utxo for further cert transactions on this cert, so we create one here
	if(wtxCertIn != NULL)
		vecSend.push_back(certRecipient);

	const vector<unsigned char> &data = theOffer.Serialize();
	CScript scriptData;
	scriptData << OP_RETURN << data;
	CRecipient fee;
	CreateFeeRecipient(scriptData, data, fee);
	vecSend.push_back(fee);
	const CWalletTx * wtxInAlias=NULL;
	const CWalletTx * wtxInEscrow=NULL;
	SendMoneySyscoin(vecSend, recipient.nAmount+fee.nAmount, false, wtx, wtxIn, wtxCertIn, wtxInAlias, wtxInEscrow);

	UniValue res(UniValue::VARR);
	res.push_back(wtx.GetHash().GetHex());
	return res;
}
UniValue offerremovewhitelist(const UniValue& params, bool fHelp) {
	if (fHelp || params.size() != 2)
		throw runtime_error(
		"offerremovewhitelist <offer guid> <cert guid>\n"
		"Remove from the whitelist of your offer(controls who can resell).\n"
						+ HelpRequiringPassphrase());
	// gather & validate inputs
	vector<unsigned char> vchOffer = vchFromValue(params[0]);
	vector<unsigned char> vchCert = vchFromValue(params[1]);


	CCert theCert;

	// this is a syscoind txn
	CWalletTx wtx;
	const CWalletTx* wtxIn;
	CScript scriptPubKeyOrig;

	EnsureWalletIsUnlocked();

	// look for a transaction with this key
	CTransaction tx;
	COffer theOffer;
	if (!GetTxOfOffer(*pofferdb, vchOffer, theOffer, tx))
		throw runtime_error("could not find an offer with this name");

	std::vector<unsigned char> vchKeyByte;
	boost::algorithm::unhex(theOffer.vchPubKey.begin(), theOffer.vchPubKey.end(), std::back_inserter(vchKeyByte));
	CPubKey currentKey(vchKeyByte);
	scriptPubKeyOrig = GetScriptForDestination(currentKey.GetID());

	wtxIn = pwalletMain->GetWalletTx(tx.GetHash());
	if (wtxIn == NULL)
		throw runtime_error("this offer is not in your wallet");
	// check for existing pending offers
	if (ExistsInMempool(vchOffer, OP_OFFER_REFUND) || ExistsInMempool(vchOffer, OP_OFFER_ACTIVATE) || ExistsInMempool(vchOffer, OP_OFFER_UPDATE)) {
		throw runtime_error("there are pending operations or refunds on that offer");
	}
	// unserialize offer from txn
	if(!theOffer.UnserializeFromTx(tx))
		throw runtime_error("cannot unserialize offer from txn");

	// get the offer from DB
	vector<COffer> vtxPos;
	if (!pofferdb->ReadOffer(vchOffer, vtxPos) || vtxPos.empty())
		throw runtime_error("could not read offer from DB");

	theOffer = vtxPos.back();
	theOffer.ClearOffer();
	theOffer.nHeight = chainActive.Tip()->nHeight;
	// create OFFERUPDATE txn keys
	CScript scriptPubKey;
	scriptPubKey << CScript::EncodeOP_N(OP_OFFER_UPDATE) << vchOffer << OP_2DROP;
	scriptPubKey += scriptPubKeyOrig;

	COfferLinkWhitelistEntry entry;
	entry.certLinkVchRand = vchCert;
	theOffer.linkWhitelist.PutWhitelistEntry(entry);

	vector<CRecipient> vecSend;
	CRecipient recipient;
	CreateRecipient(scriptPubKey, recipient);
	vecSend.push_back(recipient);
	const vector<unsigned char> &data = theOffer.Serialize();
	CScript scriptData;
	scriptData << OP_RETURN << data;
	CRecipient fee;
	CreateFeeRecipient(scriptData, data, fee);
	vecSend.push_back(fee);
	const CWalletTx * wtxInCert=NULL;
	const CWalletTx * wtxInAlias=NULL;
	const CWalletTx * wtxInEscrow=NULL;
	SendMoneySyscoin(vecSend, recipient.nAmount+fee.nAmount, false, wtx, wtxIn, wtxInCert, wtxInAlias, wtxInEscrow);

	UniValue res(UniValue::VARR);
	res.push_back(wtx.GetHash().GetHex());
	return res;
}
UniValue offerclearwhitelist(const UniValue& params, bool fHelp) {
	if (fHelp || params.size() != 1)
		throw runtime_error(
		"offerclearwhitelist <offer guid>\n"
		"Clear the whitelist of your offer(controls who can resell).\n"
						+ HelpRequiringPassphrase());
	// gather & validate inputs
	vector<unsigned char> vchOffer = vchFromValue(params[0]);

	// this is a syscoind txn
	CWalletTx wtx;
	const CWalletTx* wtxIn;
	CScript scriptPubKeyOrig;

	EnsureWalletIsUnlocked();

	// look for a transaction with this key
	CTransaction tx;
	COffer theOffer;
	if (!GetTxOfOffer(*pofferdb, vchOffer, theOffer, tx))
		throw runtime_error("could not find an offer with this name");

	std::vector<unsigned char> vchKeyByte;
	boost::algorithm::unhex(theOffer.vchPubKey.begin(), theOffer.vchPubKey.end(), std::back_inserter(vchKeyByte));
	CPubKey currentKey(vchKeyByte);
	scriptPubKeyOrig = GetScriptForDestination(currentKey.GetID());

	wtxIn = pwalletMain->GetWalletTx(tx.GetHash());
	if (wtxIn == NULL)
		throw runtime_error("this offer is not in your wallet");
	// check for existing pending offers
	if (ExistsInMempool(vchOffer, OP_OFFER_REFUND) || ExistsInMempool(vchOffer, OP_OFFER_ACTIVATE) || ExistsInMempool(vchOffer, OP_OFFER_UPDATE)) {
		throw runtime_error("there are pending operations or refunds on that offer");
	}
	// unserialize offer UniValue from txn
	if(!theOffer.UnserializeFromTx(tx))
		throw runtime_error("cannot unserialize offer from txn");

	// get the offer from DB
	vector<COffer> vtxPos;
	if (!pofferdb->ReadOffer(vchOffer, vtxPos) || vtxPos.empty())
		throw runtime_error("could not read offer from DB");

	theOffer = vtxPos.back();
	theOffer.ClearOffer();
	theOffer.nHeight = chainActive.Tip()->nHeight;
	// create OFFERUPDATE txn keys
	CScript scriptPubKey;
	scriptPubKey << CScript::EncodeOP_N(OP_OFFER_UPDATE) << vchOffer << OP_2DROP;
	scriptPubKey += scriptPubKeyOrig;

	COfferLinkWhitelistEntry entry;
	// special case to clear all entries for this offer
	entry.nDiscountPct = 127;
	theOffer.linkWhitelist.PutWhitelistEntry(entry);

	vector<CRecipient> vecSend;
	CRecipient recipient;
	CreateRecipient(scriptPubKey, recipient);
	vecSend.push_back(recipient);
	const vector<unsigned char> &data = theOffer.Serialize();
	CScript scriptData;
	scriptData << OP_RETURN << data;
	CRecipient fee;
	CreateFeeRecipient(scriptData, data, fee);
	vecSend.push_back(fee);
	const CWalletTx * wtxInCert=NULL;
	const CWalletTx * wtxInAlias=NULL;
	const CWalletTx * wtxInEscrow=NULL;
	SendMoneySyscoin(vecSend, recipient.nAmount+fee.nAmount, false, wtx, wtxIn, wtxInCert, wtxInAlias, wtxInEscrow);

	UniValue res(UniValue::VARR);
	res.push_back(wtx.GetHash().GetHex());
	return res;
}

UniValue offerwhitelist(const UniValue& params, bool fHelp) {
    if (fHelp || 1 != params.size())
        throw runtime_error("offerwhitelist <offer guid>\n"
                "List all whitelist entries for this offer.\n");
    UniValue oRes(UniValue::VARR);
    vector<unsigned char> vchOffer = vchFromValue(params[0]);
	// look for a transaction with this key
	CTransaction tx;
	COffer theOffer;
	if (!GetTxOfOffer(*pofferdb, vchOffer, theOffer, tx))
		throw runtime_error("could not find an offer with this name");
	
	for(unsigned int i=0;i<theOffer.linkWhitelist.entries.size();i++) {
		CTransaction txCert;
		CCert theCert;
		COfferLinkWhitelistEntry& entry = theOffer.linkWhitelist.entries[i];
		if (GetTxOfCert(*pcertdb, entry.certLinkVchRand, theCert, txCert))
		{
			UniValue oList(UniValue::VOBJ);
			oList.push_back(Pair("guid", stringFromVch(entry.certLinkVchRand)));
			oList.push_back(Pair("title", stringFromVch(theCert.vchTitle)));
			oList.push_back(Pair("ismine", IsSyscoinTxMine(txCert) ? "true" : "false"));
			std::vector<unsigned char> vchKeyByte;
			boost::algorithm::unhex(theCert.vchPubKey.begin(), theCert.vchPubKey.end(), std::back_inserter(vchKeyByte));
			CPubKey PubKey(vchKeyByte);
			CSyscoinAddress address(PubKey.GetID());
			address = CSyscoinAddress(address.ToString());
			if(!address.IsValid())
				continue;
			oList.push_back(Pair("address", address.ToString()));
			int expires_in = 0;
			uint64_t nHeight = theCert.nHeight;
			if (!GetSyscoinTransaction(nHeight, txCert.GetHash(), txCert, Params().GetConsensus()))
				continue;
			
            if(nHeight + GetCertExpirationDepth() - chainActive.Tip()->nHeight > 0)
			{
				expires_in = nHeight + GetCertExpirationDepth() - chainActive.Tip()->nHeight;
			}  
			oList.push_back(Pair("expiresin",expires_in));
			oList.push_back(Pair("offer_discount_percentage", strprintf("%d%%", entry.nDiscountPct)));
			oRes.push_back(oList);
		}  
    }
    return oRes;
}
UniValue offerupdate(const UniValue& params, bool fHelp) {
	if (fHelp || params.size() < 5 || params.size() > 9)
		throw runtime_error(
		"offerupdate <guid> <category> <title> <quantity> <price> [description] [private=0] [cert. guid] [exclusive resell=1]\n"
						"Perform an update on an offer you control.\n"
						+ HelpRequiringPassphrase());
	// gather & validate inputs
	vector<unsigned char> vchOffer = vchFromValue(params[0]);
	vector<unsigned char> vchCat = vchFromValue(params[1]);
	vector<unsigned char> vchTitle = vchFromValue(params[2]);
	vector<unsigned char> vchDesc;
	vector<unsigned char> vchCert;
	bool bExclusiveResell = true;
	int bPrivate = false;
	unsigned int nQty;
	double price;
	if (params.size() >= 6) vchDesc = vchFromValue(params[5]);
	if (params.size() >= 7) bPrivate = atoi(params[6].get_str().c_str()) == 1? true: false;
	if (params.size() >= 8) vchCert = vchFromValue(params[7]);
	if(params.size() >= 9) bExclusiveResell = atoi(params[8].get_str().c_str()) == 1? true: false;
	if(atof(params[3].get_str().c_str()) < 0)
		throw runtime_error("invalid quantity value, must be greator than 0");
	
	try {
		nQty = boost::lexical_cast<unsigned int>(params[3].get_str());
		price = atof(params[4].get_str().c_str());

	} catch (std::exception &e) {
		throw runtime_error("invalid price and/or quantity values. Quantity must be less than 4294967296.");
	}
	if (params.size() >= 6) vchDesc = vchFromValue(params[5]);
	if(price <= 0)
	{
		throw runtime_error("offer price must be greater than 0!");
	}
	
	if(vchCat.size() < 1)
        throw runtime_error("offer category cannot by empty!");
	if(vchTitle.size() < 1)
        throw runtime_error("offer title cannot be empty!");
	if(vchCat.size() > 255)
        throw runtime_error("offer category cannot exceed 255 bytes!");
	if(vchTitle.size() > 255)
        throw runtime_error("offer title cannot exceed 255 bytes!");
    // 1kbyte offer desc. maxlen
	if (vchDesc.size() > MAX_VALUE_LENGTH)
		throw runtime_error("offer description cannot exceed 1023 bytes!");

	// this is a syscoind txn
	CWalletTx wtx;
	const CWalletTx* wtxIn;
	CScript scriptPubKeyOrig, scriptPubKeyCertOrig;

	EnsureWalletIsUnlocked();

	// look for a transaction with this key
	CTransaction tx, linktx;
	COffer theOffer, linkOffer;
	if (!GetTxOfOffer(*pofferdb, vchOffer, theOffer, tx))
		throw runtime_error("could not find an offer with this name");

	std::vector<unsigned char> vchKeyByte;
	boost::algorithm::unhex(theOffer.vchPubKey.begin(), theOffer.vchPubKey.end(), std::back_inserter(vchKeyByte));
	CPubKey currentKey(vchKeyByte);
	scriptPubKeyOrig = GetScriptForDestination(currentKey.GetID());

	// create OFFERUPDATE, CERTUPDATE txn keys
	CScript scriptPubKey, scriptPubKeyCert;
	scriptPubKey << CScript::EncodeOP_N(OP_OFFER_UPDATE) << vchOffer << OP_2DROP;
	scriptPubKey += scriptPubKeyOrig;

	wtxIn = pwalletMain->GetWalletTx(tx.GetHash());
	if (wtxIn == NULL)
		throw runtime_error("this offer is not in your wallet");
	// check for existing pending offers
	if (ExistsInMempool(vchOffer, OP_OFFER_REFUND) || ExistsInMempool(vchOffer, OP_OFFER_ACTIVATE) || ExistsInMempool(vchOffer, OP_OFFER_UPDATE)) {
		throw runtime_error("there are pending operations or refunds on that offer");
	}
	// unserialize offer UniValue from txn
	if(!theOffer.UnserializeFromTx(tx))
		throw runtime_error("cannot unserialize offer from txn");

	// get the offer from DB
	vector<COffer> vtxPos;
	if (!pofferdb->ReadOffer(vchOffer, vtxPos) || vtxPos.empty())
		throw runtime_error("could not read offer from DB");
	CCert theCert;
	CTransaction txCert;
	const CWalletTx *wtxCertIn = NULL;
	// make sure this cert is still valid
	if (GetTxOfCert(*pcertdb, vchCert, theCert, txCert))
	{
		vector<vector<unsigned char> > vvch;
		wtxCertIn = pwalletMain->GetWalletTx(txCert.GetHash());
		// make sure its in your wallet (you control this cert)		
		if (!IsSyscoinTxMine(txCert) || wtxCertIn == NULL) 
			throw runtime_error("Cannot sell this certificate, it is not yours!");
		int op, nOut;
		if(DecodeCertTx(txCert, op, nOut, vvch))
			vchCert = vvch[0];

		std::vector<unsigned char> vchCertKeyByte;
		boost::algorithm::unhex(theCert.vchPubKey.begin(), theCert.vchPubKey.end(), std::back_inserter(vchCertKeyByte));
		CPubKey currentCertKey(vchCertKeyByte);
		scriptPubKeyCertOrig = GetScriptForDestination(currentCertKey.GetID());
	}


	scriptPubKeyCert << CScript::EncodeOP_N(OP_CERT_UPDATE) << vchCert << OP_2DROP;
	scriptPubKeyCert += scriptPubKeyCertOrig;
	theOffer = vtxPos.back();
	COffer offerCopy = theOffer;
	theOffer.ClearOffer();	
	int precision = 2;
	// get precision
	convertCurrencyCodeToSyscoin(offerCopy.sCurrencyCode, offerCopy.GetPrice(), chainActive.Tip()->nHeight, precision);
	double minPrice = pow(10.0,-precision);
	if(price < minPrice)
		price = minPrice;
	// update offer values
	if(offerCopy.sCategory != vchCat)
		theOffer.sCategory = vchCat;
	if(offerCopy.sTitle != vchTitle)
		theOffer.sTitle = vchTitle;
	if(offerCopy.sDescription != vchDesc)
		theOffer.sDescription = vchDesc;
	// update pubkey to new cert if we change the cert we are selling for this offer or remove it
	if(wtxCertIn != NULL)
	{
		theOffer.vchCert = vchCert;
		theOffer.vchPubKey = theCert.vchPubKey;
	}
	// else if we remove the cert from this offer (don't sell a cert) then clear the cert
	else
		theOffer.vchCert.clear();

	if (params.size() >= 7)
		theOffer.bPrivate = bPrivate;
	unsigned int memPoolQty = QtyOfPendingAcceptsInMempool(vchOffer);
	if((nQty-memPoolQty) < 0)
		throw runtime_error("not enough remaining quantity to fulfill this offerupdate"); // SS i think needs better msg
	theOffer.nHeight = chainActive.Tip()->nHeight;
	theOffer.SetPrice(price);
	
	if(params.size() >= 9)
		theOffer.linkWhitelist.bExclusiveResell = bExclusiveResell;


	vector<CRecipient> vecSend;
	CRecipient recipient;
	CreateRecipient(scriptPubKey, recipient);
	vecSend.push_back(recipient);
	CRecipient certRecipient;
	CreateRecipient(scriptPubKeyCert, certRecipient);
	// if we use a cert as input to this offer tx, we need another utxo for further cert transactions on this cert, so we create one here
	if(wtxCertIn != NULL)
		vecSend.push_back(certRecipient);

	const vector<unsigned char> &data = theOffer.Serialize();
	CScript scriptData;
	scriptData << OP_RETURN << data;
	CRecipient fee;
	CreateFeeRecipient(scriptData, data, fee);
	vecSend.push_back(fee);
	const CWalletTx * wtxInAlias=NULL;
	const CWalletTx * wtxInEscrow=NULL;
	SendMoneySyscoin(vecSend, recipient.nAmount+fee.nAmount, false, wtx, wtxIn, wtxCertIn, wtxInAlias, wtxInEscrow);

	UniValue res(UniValue::VARR);
	res.push_back(wtx.GetHash().GetHex());
	return res;
}

UniValue offerrefund(const UniValue& params, bool fHelp) {
	if (fHelp || 2 != params.size())
		throw runtime_error("offerrefund <offerguid> <acceptguid>\n"
				"Refund an offer accept of an offer you control.\n"
				"<offerguid> guidkey of offer.\n"
				"<acceptguid> guidkey of offer accept of offer to refund.\n"
				+ HelpRequiringPassphrase());
	const vector<unsigned char> &vchAcceptRand = vchFromString(params[1].get_str());
	const vector<unsigned char> &vchOffer = vchFromString(params[0].get_str());

	EnsureWalletIsUnlocked();

	// look for a transaction with this key
	CTransaction txOffer;
	COffer theOffer;
	uint256 blockHash;

	if (!GetTxOfOffer(*pofferdb, vchOffer, theOffer, txOffer))
		throw runtime_error("could not find an offer with this identifier");
	vector<vector<unsigned char> > vvch;
	int op, nOut;

	if(!DecodeOfferTx(txOffer, op, nOut, vvch)) 
	{
		string err = "could not decode offeraccept tx with hash: " +  txOffer.GetHash().GetHex();
        throw runtime_error(err.c_str());
	}

	const CWalletTx *wtxIn;
	wtxIn = pwalletMain->GetWalletTx(txOffer.GetHash());
	if (wtxIn == NULL)
	{
		throw runtime_error("can't find this offer in your wallet");
	}

	if (ExistsInMempool(vchAcceptRand, OP_OFFER_REFUND) || ExistsInMempool(vchAcceptRand, OP_OFFER_ACTIVATE) || ExistsInMempool(vvch[0], OP_OFFER_UPDATE)) {
		throw runtime_error("there are pending operations or refunds on that offer");
	}

	// check if this offer is linked to another offer
	if (!theOffer.vchLinkOffer.empty())
		throw runtime_error("You cannot refund an offer that is linked to another offer, only the owner of the original offer can issue a refund.");
	
	string strError = makeOfferRefundTX(txOffer, vchOffer, vchAcceptRand, OFFER_REFUND_PAYMENT_INPROGRESS);
	if (strError != "")
	{
		throw runtime_error(strError);
	}
	return "Success";
}

UniValue offeraccept(const UniValue& params, bool fHelp) {
	if (fHelp || 1 > params.size() || params.size() > 9)
		throw runtime_error("offeraccept <guid> [quantity] [message] [refund address] [BTC TxId] [linkedguid] [linkedacceptguid] [escrowTxHash]\n"
				"Accept&Pay for a confirmed offer.\n"
				"<guid> guidkey from offer.\n"
				"<quantity> quantity to buy. Defaults to 1.\n"
				"<message> payment message to seller, 1KB max.\n"
				"<refund address> In case offer not accepted refund to this address. Leave empty to use a new address from your wallet. \n"
				"<BTC TxId> If you have paid in Bitcoin and the offer is in Bitcoin, enter the transaction ID here. Default is empty.\n"
				"<linkedguid> guidkey from offer linking to offer accept in <linkedacceptguid>. For internal use only, leave blank\n"
				"<linkedacceptguid> guidkey from offer accept linking to this offer accept. For internal use only, leave blank\n"
				"<escrowTxHash> If this offer accept is done by an escrow release. For internal use only, leave blank\n"
				+ HelpRequiringPassphrase());
	vector<unsigned char> vchRefundAddress;	
	CSyscoinAddress refundAddr;	
	vector<unsigned char> vchOffer = vchFromValue(params[0]);
	vector<unsigned char> vchPubKey;
	vector<unsigned char> vchBTCTxId = vchFromValue(params.size()>=5?params[4]:"");
	vector<unsigned char> vchLinkOffer = vchFromValue(params.size()>= 6? params[5]:"");
	vector<unsigned char> vchLinkOfferAccept = vchFromValue(params.size()>= 7? params[6]:"");
	vector<unsigned char> vchMessage = vchFromValue(params.size()>=3?params[2]:"");
	vector<unsigned char> vchEscrowTxHash = vchFromValue(params.size()>=8?params[7]:"");
	int64_t nHeight = 0;
	unsigned int nQty = 1;
	if (params.size() >= 2) {
		if(atof(params[1].get_str().c_str()) < 0)
			throw runtime_error("invalid quantity value, must be greator than 0");
	
		try {
			nQty = boost::lexical_cast<unsigned int>(params[1].get_str());
		} catch (std::exception &e) {
			throw runtime_error("invalid quantity value. Quantity must be less than 4294967296.");
		}
	}
	if(params.size() < 4 || params[3].size() <= 0)
	{
		CPubKey newDefaultKey;
		pwalletMain->GetKeyFromPool(newDefaultKey);
		refundAddr = CSyscoinAddress(newDefaultKey.GetID());
		vchRefundAddress = vchFromString(refundAddr.ToString());
		if(!refundAddr.IsValid())
			throw runtime_error("Refund address is not valid!");
		std::vector<unsigned char> vchKey(newDefaultKey.begin(), newDefaultKey.end());
		vchPubKey = vchFromString(HexStr(vchKey));
	}
	else
	{
		vchRefundAddress = vchFromValue(params[3]);
		refundAddr = CSyscoinAddress(stringFromVch(vchRefundAddress));
		if(!refundAddr.IsValid())
			throw runtime_error("Refund address is not valid!");
        CKeyID keyID;
        if (!refundAddr.GetKeyID(keyID))
            throw runtime_error("Refund address does not refer to a key!");
        CPubKey PubKey;
        if (!pwalletMain->GetPubKey(keyID, PubKey))
            throw runtime_error("Refund address does not refer to a public key!");
        if (!PubKey.IsFullyValid())
            throw runtime_error("Refund address refers to an invalid public key!");
		std::vector<unsigned char> vchKey(PubKey.begin(), PubKey.end());
		vchPubKey = vchFromString(HexStr(vchKey));
	}

    if (vchMessage.size() <= 0 && vchPubKey.empty())
        throw runtime_error("offeraccept message data cannot be empty!");

	// this is a syscoin txn
	CWalletTx wtx;
	CScript scriptPubKeyOrig, scriptPubKeyCertOrig;

	// generate offer accept identifier and hash
	int64_t rand = GetRand(std::numeric_limits<int64_t>::max());
	vector<unsigned char> vchAcceptRand = CScriptNum(rand).getvch();
	vector<unsigned char> vchAccept = vchFromString(HexStr(vchAcceptRand));

	// create OFFERACCEPT txn keys
	CScript scriptPubKey;
	CScript scriptPubKeyCert;
	scriptPubKey << CScript::EncodeOP_N(OP_OFFER_ACCEPT) << vchOffer << vchAccept << OP_2DROP << OP_DROP;

	EnsureWalletIsUnlocked();
	const CWalletTx *wtxEscrowIn = NULL;
	CEscrow escrow;
	vector<vector<unsigned char> > escrowVvch;
	if(!vchEscrowTxHash.empty())
	{
		if(!vchBTCTxId.empty())
			throw runtime_error("Cannot release an escrow transaction by paying in Bitcoins");
		uint256 escrowTxHash(uint256S(stringFromVch(vchEscrowTxHash)));
		// make sure escrow is in wallet
		wtxEscrowIn = pwalletMain->GetWalletTx(escrowTxHash);
		if (wtxEscrowIn == NULL) 
			throw runtime_error("release escrow transaction is not in your wallet");;
		if(!escrow.UnserializeFromTx(*wtxEscrowIn))
			throw runtime_error("release escrow transaction cannot unserialize escrow value");
		
		int op, nOut;
		if (!DecodeEscrowTx(*wtxEscrowIn, op, nOut, escrowVvch))
			throw runtime_error("Cannot decode escrow tx hash");
		// if we want to accept an escrow release or we are accepting a linked offer from an escrow release. Override heightToCheckAgainst if using escrow since escrow can take a long time.
		// get escrow activation
		vector<CEscrow> escrowVtxPos;
		if (pescrowdb->ExistsEscrow(escrowVvch[0])) {
			if (pescrowdb->ReadEscrow(escrowVvch[0], escrowVtxPos) && !escrowVtxPos.empty())
			{	
				// we want the initial funding escrow transaction height as when to calculate this offer accept price from convertCurrencyCodeToSyscoin()
				CEscrow fundingEscrow = escrowVtxPos.front();
				nHeight = fundingEscrow.nHeight;
			}
		}	
	}
	if (ExistsInMempool(vchOffer, OP_OFFER_REFUND) || ExistsInMempool(vchOffer, OP_OFFER_ACTIVATE) || ExistsInMempool(vchOffer, OP_OFFER_UPDATE)) {
		throw runtime_error("there are pending operations or refunds on that offer");
	}
	// look for a transaction with this key
	CTransaction tx, acceptTx;
	COffer theOffer;
	COfferAccept theLinkedOfferAccept;
	if (!GetTxOfOffer(*pofferdb, vchOffer, theOffer, tx))
	{
		throw runtime_error("could not find an offer with this identifier");
	}
	// if this is a linked offer accept, set the height to the first height so sys_rates price will match what it was at the time of the original accept
	if (!vchLinkOfferAccept.empty())
	{
		if(GetTxOfOfferAccept(*pofferdb, vchOffer, vchLinkOfferAccept, theLinkedOfferAccept, acceptTx))
			nHeight = theLinkedOfferAccept.nHeight;
		else
			throw runtime_error("could not find an offer accept with this identifier");
	}

	COffer linkedOffer;
	CTransaction tmpTx;
	// check if parent to linked offer is still valid
	if (!theOffer.vchLinkOffer.empty())
	{
		if(!vchBTCTxId.empty())
			throw runtime_error("Cannot accept a linked offer by paying in Bitcoins");

		if(pofferdb->ExistsOffer(theOffer.vchLinkOffer))
		{
			if (!GetTxOfOffer(*pofferdb, theOffer.vchLinkOffer, linkedOffer, tmpTx))
				throw runtime_error("Trying to accept a linked offer but could not find parent offer, perhaps it is expired");
			if (linkedOffer.bOnlyAcceptBTC)
				throw runtime_error("Linked offer only accepts Bitcoins, linked offers currently only work with Syscoin payments");
		}
	}
	COfferLinkWhitelistEntry foundCert;
	const CWalletTx *wtxCertIn = NULL;
	vector<unsigned char> vchCert;
	// go through the whitelist and see if you own any of the certs to apply to this offer for a discount
	for(unsigned int i=0;i<theOffer.linkWhitelist.entries.size();i++) {
		CTransaction txCert;
		
		CCert theCert;
		vector<vector<unsigned char> > vvch;
		COfferLinkWhitelistEntry& entry = theOffer.linkWhitelist.entries[i];
		// make sure this cert is still valid
		if (GetTxOfCert(*pcertdb, entry.certLinkVchRand, theCert, txCert))
		{
			// make sure its in your wallet (you control this cert)
			wtxCertIn = pwalletMain->GetWalletTx(txCert.GetHash());		
			if (IsSyscoinTxMine(txCert) && wtxCertIn != NULL) 
			{
				foundCert = entry;		
				int op, nOut;
				if(DecodeCertTx(txCert, op, nOut, vvch))
					vchCert = vvch[0];
				std::vector<unsigned char> vchCertKeyByte;
				boost::algorithm::unhex(theCert.vchPubKey.begin(), theCert.vchPubKey.end(), std::back_inserter(vchCertKeyByte));
				CPubKey currentCertKey(vchCertKeyByte);
				scriptPubKeyCertOrig = GetScriptForDestination(currentCertKey.GetID());
				if(theCert.vchPubKey != theOffer.vchPubKey && wtxEscrowIn == NULL)
					throw runtime_error("cannot purchase this offer because the certificate has been transferred since offer was created or it is linked to another offer");
			}		
		}
	}
	scriptPubKeyCert << CScript::EncodeOP_N(OP_CERT_UPDATE) << vchCert << OP_2DROP;
	scriptPubKeyCert += scriptPubKeyCertOrig;
	// if this is an accept for a linked offer, the offer is set to exclusive mode and you dont have a cert in the whitelist, you cannot accept this offer
	if(!vchLinkOfferAccept.empty() && foundCert.IsNull() && theOffer.linkWhitelist.bExclusiveResell)
	{
		throw runtime_error("cannot pay for this linked offer because you don't own a cert from its whitelist");
	}

	unsigned int memPoolQty = QtyOfPendingAcceptsInMempool(vchOffer);
	if(theOffer.nQty < (nQty+memPoolQty))
		throw runtime_error("not enough remaining quantity to fulfill this orderaccept");

	int precision = 2;
	CAmount nPrice = convertCurrencyCodeToSyscoin(theOffer.sCurrencyCode, theOffer.GetPrice(foundCert), nHeight>0?nHeight:chainActive.Tip()->nHeight, precision);
	string strCipherText = "";
	// encryption should only happen once even when not a resell or not an escrow accept. It is already encrypted in both cases.
	if(vchLinkOfferAccept.empty() && vchEscrowTxHash.empty())
	{
		// encrypt to offer owner
		if(!EncryptMessage(theOffer.vchPubKey, vchMessage, strCipherText))
			throw runtime_error("could not encrypt message to seller");
	}
	if(!vchLinkOfferAccept.empty() && !vchBTCTxId.empty())
		throw runtime_error("Cannot accept a linked offer by paying in Bitcoins");

	if (strCipherText.size() > MAX_ENCRYPTED_VALUE_LENGTH)
		throw runtime_error("offeraccept message length cannot exceed 1023 bytes!");

	if((vchLinkOfferAccept.empty() && !vchLinkOffer.empty()) || (vchLinkOffer.empty() && !vchLinkOfferAccept.empty()))
		throw runtime_error("If you are accepting a linked offer you must provide the offer guid AND the offer accept guid");

	if(!theOffer.vchCert.empty())
	{
		if(!vchBTCTxId.empty())
			throw runtime_error("Cannot purchase certificates with Bitcoins!");
		CTransaction txCert;
		CCert theCert;
		// make sure this cert is still valid
		if (!GetTxOfCert(*pcertdb, theOffer.vchCert, theCert, txCert))
			throw runtime_error("Cannot purchase with this certificate, it may be expired!");
		if (!IsSyscoinTxMine(txCert)) 
			throw runtime_error("Cannot purchase with this certificate, it is not yours!");
	}
	// create accept
	COfferAccept txAccept;
	txAccept.vchAcceptRand = vchAccept;
	if(strCipherText.size() > 0)
		txAccept.vchMessage = vchFromString(strCipherText);
	else
		txAccept.vchMessage = vchMessage;
	txAccept.nQty = nQty;
	txAccept.nPrice = theOffer.GetPrice(foundCert);
	txAccept.vchLinkOfferAccept = vchLinkOfferAccept;
	txAccept.vchLinkOffer = vchLinkOffer;
	txAccept.vchRefundAddress = vchRefundAddress;
	// if we have a linked offer accept then use height from linked accept (the one buyer makes, not the reseller). We need to do this to make sure we convert price at the time of initial buyer's accept.
	// in checkescrowinput we override this if its from an escrow release, just like above.
	txAccept.nHeight = nHeight>0?nHeight:chainActive.Tip()->nHeight;

	txAccept.bPaid = true;
	txAccept.vchBuyerKey = vchPubKey;

    CAmount nTotalValue = ( nPrice * nQty );
    

    CScript scriptPayment;
	std::vector<unsigned char> vchKeyByte;
	boost::algorithm::unhex(theOffer.vchPubKey.begin(), theOffer.vchPubKey.end(), std::back_inserter(vchKeyByte));
	CPubKey currentKey(vchKeyByte);
	scriptPayment = GetScriptForDestination(currentKey.GetID());
	scriptPubKey += scriptPayment;

	vector<CRecipient> vecSend;
	CRecipient paymentRecipient = {scriptPubKey, nTotalValue, false};
	CRecipient certRecipient;
	CreateRecipient(scriptPubKeyCert, certRecipient);
	// if we use a cert as input to this offer tx, we need another utxo for further cert transactions on this cert, so we create one here
	if(wtxCertIn != NULL)
		vecSend.push_back(certRecipient);

	// check for Bitcoin payment on the bitcoin network, otherwise pay in syscoin
	if(!vchBTCTxId.empty() && stringFromVch(theOffer.sCurrencyCode) == "BTC")
	{
		uint256 txBTCId(uint256S(stringFromVch(vchBTCTxId)));
		txAccept.txBTCId = txBTCId;
		// consult a block explorer for the btc txid and check to see if it pays offer address with correct amount
		throw runtime_error("not implemented");
	}
	else if(!theOffer.bOnlyAcceptBTC)
	{
		vecSend.push_back(paymentRecipient);
	}
	else
	{
		throw runtime_error("This offer must be paid with Bitcoins as per requirements of the seller");
	}
	theOffer.ClearOffer();
	theOffer.accept = txAccept;


	const vector<unsigned char> &data = theOffer.Serialize();
	CScript scriptData;
	scriptData << OP_RETURN << data;
	CRecipient fee;
	CreateFeeRecipient(scriptData, data, fee);
	vecSend.push_back(fee);
	const CWalletTx * wtxInAlias=NULL;
	const CWalletTx * wtxInOffer=NULL;
	// if making a purchase and we are using a certificate from the whitelist of the offer, we may need to prove that we own that certificate so in that case we attach an input from the certificate
	// if purchasing an escrow, we adjust the height to figure out pricing of the accept so we may also attach escrow inputs to the tx
	SendMoneySyscoin(vecSend, paymentRecipient.nAmount+fee.nAmount, false, wtx, wtxInOffer, wtxCertIn, wtxInAlias, wtxEscrowIn);
	
	UniValue res(UniValue::VARR);
	res.push_back(wtx.GetHash().GetHex());
	res.push_back(stringFromVch(vchAccept));
	return res;
}

UniValue offerinfo(const UniValue& params, bool fHelp) {
	if (fHelp || 1 != params.size())
		throw runtime_error("offerinfo <guid>\n"
				"Show values of an offer.\n");

	UniValue oLastOffer(UniValue::VOBJ);
	vector<unsigned char> vchOffer = vchFromValue(params[0]);
	map< vector<unsigned char>, int > vNamesI;
	string offer = stringFromVch(vchOffer);
	{
		vector<COffer> vtxPos;
		if (!pofferdb->ReadOffer(vchOffer, vtxPos))
			throw runtime_error("failed to read from offer DB");
		if (vtxPos.size() < 1)
			throw runtime_error("no result returned");

        // get transaction pointed to by offer
        CTransaction tx;
        uint256 txHash = vtxPos.back().txHash;
        if (!GetSyscoinTransaction(vtxPos.back().nHeight, txHash, tx, Params().GetConsensus()))
            throw runtime_error("failed to read offer transaction from disk");

        COffer theOffer = vtxPos.back();

		UniValue oOffer(UniValue::VOBJ);
		vector<unsigned char> vchValue;
		UniValue aoOfferAccepts(UniValue::VARR);
		for(int i=vtxPos.size()-1;i>=0;i--) {
			COfferAccept ca = vtxPos[i].accept;
			if(ca.IsNull())
				continue;
			// get last active accept only
			if (vNamesI.find(ca.vchAcceptRand) != vNamesI.end() && (ca.nHeight <= vNamesI[ca.vchAcceptRand] || vNamesI[ca.vchAcceptRand] < 0))
				continue;
			vNamesI[ca.vchAcceptRand] = ca.nHeight;
			UniValue oOfferAccept(UniValue::VOBJ);

	        // get transaction pointed to by offer

	        CTransaction txA;
	        uint256 txHashA= ca.txHash;
	        if (!GetSyscoinTransaction(ca.nHeight, txHashA, txA, Params().GetConsensus()))
			{
				error(strprintf("failed to accept read transaction from disk: %s", txHashA.GetHex()).c_str());
				continue;
			}
			vector<vector<unsigned char> > vvch;
            int op, nOut;
            if (!DecodeOfferTx(txA, op, nOut, vvch) 
            	|| !IsOfferOp(op) 
            	|| (op != OP_OFFER_ACCEPT && op != OP_OFFER_REFUND))
                continue;
			const vector<unsigned char> &vchAcceptRand = vvch[1];	
			string sTime;
			CBlockIndex *pindex = chainActive[ca.nHeight];
			if (pindex) {
				sTime = strprintf("%llu", pindex->nTime);
			}
            string sHeight = strprintf("%llu", ca.nHeight);
			oOfferAccept.push_back(Pair("id", stringFromVch(vchAcceptRand)));
			oOfferAccept.push_back(Pair("linkofferaccept", stringFromVch(ca.vchLinkOfferAccept)));
			oOfferAccept.push_back(Pair("linkoffer", stringFromVch(ca.vchLinkOffer)));
			oOfferAccept.push_back(Pair("txid", ca.txHash.GetHex()));
			oOfferAccept.push_back(Pair("btctxid", ca.txBTCId.GetHex()));
			oOfferAccept.push_back(Pair("height", sHeight));
			oOfferAccept.push_back(Pair("time", sTime));
			oOfferAccept.push_back(Pair("quantity", strprintf("%u", ca.nQty)));
			oOfferAccept.push_back(Pair("currency", stringFromVch(theOffer.sCurrencyCode)));
			int precision = 2;
			CAmount nPricePerUnit = convertCurrencyCodeToSyscoin(theOffer.sCurrencyCode, ca.nPrice, ca.nHeight, precision);
			oOfferAccept.push_back(Pair("systotal", ValueFromAmount(nPricePerUnit * ca.nQty)));
			oOfferAccept.push_back(Pair("sysprice", ValueFromAmount(nPricePerUnit)));
			oOfferAccept.push_back(Pair("price", strprintf("%.*f", precision, ca.nPrice ))); 	
			oOfferAccept.push_back(Pair("total", strprintf("%.*f", precision, ca.nPrice * ca.nQty )));
			COfferLinkWhitelistEntry entry;
			
			if(IsSyscoinTxMine(tx)) 
			{
				vector<unsigned char> vchOfferLink;
				bool foundOffer = false;
				for (unsigned int i = 0; i < tx.vin.size(); i++) {
					vector<vector<unsigned char> > vvchIn;
					int opIn;
					const COutPoint *prevOutput = &tx.vin[i].prevout;
					GetPreviousInput(prevOutput, opIn, vvchIn);
					if(foundOffer)
						break;

					if (!foundOffer && IsOfferOp(opIn)) {
						foundOffer = true; 
						vchOfferLink = vvchIn[0];
					}
				}
				if(foundOffer)
					theOffer.linkWhitelist.GetLinkEntryByHash(vchOfferLink, entry);
			}
			oOfferAccept.push_back(Pair("offer_discount_percentage", strprintf("%d%%", entry.nDiscountPct)));
			oOfferAccept.push_back(Pair("ismine", IsSyscoinTxMine(txA) ? "true" : "false"));
			if(ca.bPaid) {
				if(!ca.txBTCId.IsNull())
					oOfferAccept.push_back(Pair("paid","check"));
				else
					oOfferAccept.push_back(Pair("paid","true"));
				string strMessage = string("");
				if(!DecryptMessage(theOffer.vchPubKey, ca.vchMessage, strMessage))
					strMessage = string("Encrypted for owner of offer");
				oOfferAccept.push_back(Pair("pay_message", strMessage));

			}
			else
			{
				oOfferAccept.push_back(Pair("paid","false"));
				oOfferAccept.push_back(Pair("pay_message",""));
			}
			if(ca.bRefunded) { 
				oOfferAccept.push_back(Pair("refunded", "true"));
			}
			else
			{
				oOfferAccept.push_back(Pair("refunded", "false"));
			}
			
			

			aoOfferAccepts.push_back(oOfferAccept);
		}
		uint64_t nHeight;
		int expired;
		int expires_in;
		int expired_block;
  
		expired = 0;	
		expires_in = 0;
		expired_block = 0;
        nHeight = theOffer.nHeight;
		oOffer.push_back(Pair("offer", offer));
		oOffer.push_back(Pair("cert", stringFromVch(theOffer.vchCert)));
		oOffer.push_back(Pair("txid", tx.GetHash().GetHex()));
		expired_block = nHeight + GetOfferExpirationDepth();
        if(nHeight + GetOfferExpirationDepth() - chainActive.Tip()->nHeight <= 0)
		{
			expired = 1;
		}  
		if(expired == 0)
		{
			expires_in = nHeight + GetOfferExpirationDepth() - chainActive.Tip()->nHeight;
		}
		oOffer.push_back(Pair("expires_in", expires_in));
		oOffer.push_back(Pair("expired_block", expired_block));
		oOffer.push_back(Pair("expired", expired));
		oOffer.push_back(Pair("height", strprintf("%llu", nHeight)));
		std::vector<unsigned char> vchSellerKeyByte;
		boost::algorithm::unhex(theOffer.vchPubKey.begin(), theOffer.vchPubKey.end(), std::back_inserter(vchSellerKeyByte));
		CPubKey SellerPubKey(vchSellerKeyByte);
		CSyscoinAddress selleraddy(SellerPubKey.GetID());
		selleraddy = CSyscoinAddress(selleraddy.ToString());
		if(!selleraddy.IsValid() || !selleraddy.isAlias)
			continue;
		oOffer.push_back(Pair("address", selleraddy.ToString()));
		oOffer.push_back(Pair("category", stringFromVch(theOffer.sCategory)));
		oOffer.push_back(Pair("title", stringFromVch(theOffer.sTitle)));
		oOffer.push_back(Pair("quantity", strprintf("%u", theOffer.nQty)));
		oOffer.push_back(Pair("currency", stringFromVch(theOffer.sCurrencyCode)));
		
		
		int precision = 2;
		CAmount nPricePerUnit = convertCurrencyCodeToSyscoin(theOffer.sCurrencyCode, theOffer.GetPrice(), nHeight, precision);
		oOffer.push_back(Pair("sysprice", ValueFromAmount(nPricePerUnit)));
		oOffer.push_back(Pair("price", strprintf("%.*f", precision, theOffer.GetPrice() ))); 
		
		oOffer.push_back(Pair("ismine", IsSyscoinTxMine(tx) ? "true" : "false"));
		if(!theOffer.vchLinkOffer.empty() && IsSyscoinTxMine(tx)) {
			oOffer.push_back(Pair("commission", strprintf("%d%%", theOffer.nCommission)));
			oOffer.push_back(Pair("offerlink", "true"));
			oOffer.push_back(Pair("offerlink_guid", stringFromVch(theOffer.vchLinkOffer)));
		}
		else
		{
			oOffer.push_back(Pair("commission", "0"));
			oOffer.push_back(Pair("offerlink", "false"));
			oOffer.push_back(Pair("offerlink_guid", "NA"));
		}
		oOffer.push_back(Pair("exclusive_resell", theOffer.linkWhitelist.bExclusiveResell ? "ON" : "OFF"));
		oOffer.push_back(Pair("private", theOffer.bPrivate ? "Yes" : "No"));
		oOffer.push_back(Pair("btconly", theOffer.bOnlyAcceptBTC ? "Yes" : "No"));
		oOffer.push_back(Pair("description", stringFromVch(theOffer.sDescription)));
		oOffer.push_back(Pair("alias", selleraddy.aliasName));
		oOffer.push_back(Pair("accepts", aoOfferAccepts));
		oLastOffer = oOffer;
	}
	return oLastOffer;

}
UniValue offeracceptlist(const UniValue& params, bool fHelp) {
    if (fHelp || 1 < params.size())
		throw runtime_error("offeracceptlist [offer]\n"
				"list my offer accepts");

    vector<unsigned char> vchOffer;
	vector<unsigned char> vchOfferToFind;

    if (params.size() == 1)
        vchOfferToFind = vchFromValue(params[0]);
    map< vector<unsigned char>, int > vNamesI;
	vector<unsigned char> vchEscrow;	
    UniValue oRes(UniValue::VARR);
    {

        uint256 blockHash;
        uint256 hash;
        CTransaction tx;
		
        BOOST_FOREACH(PAIRTYPE(const uint256, CWalletTx)& item, pwalletMain->mapWallet)
        {
			UniValue oOfferAccept(UniValue::VOBJ);
            // get txn hash, read txn index
            hash = item.second.GetHash();

 			const CWalletTx &wtx = item.second;

            // skip non-syscoin txns
            if (wtx.nVersion != SYSCOIN_TX_VERSION)
                continue;

            // decode txn, skip non-alias txns
            vector<vector<unsigned char> > vvch;
            int op, nOut;
            if (!DecodeOfferTx(wtx, op, nOut, vvch) 
            	|| !IsOfferOp(op) 
            	|| (op != OP_OFFER_ACCEPT && op != OP_OFFER_REFUND))
                continue;
			if(vvch[0] != vchOfferToFind && !vchOfferToFind.empty())
				continue;
            vchOffer = vvch[0];
			
			COfferAccept theOfferAccept;

			// Check hash
			const vector<unsigned char> &vchAcceptRand = vvch[1];			
			CTransaction offerTx, acceptTx;
			COffer theOffer;

			if(!GetTxOfOffer(*pofferdb, vchOffer, theOffer, offerTx))	
				continue;
			if (!GetTxOfOfferAccept(*pofferdb, vchOffer, vchAcceptRand, theOfferAccept, acceptTx))
				continue;
			if(theOfferAccept.vchAcceptRand != vchAcceptRand)
				continue;
			// get last active accept only
			if (vNamesI.find(vchAcceptRand) != vNamesI.end() && (theOfferAccept.nHeight <= vNamesI[vchAcceptRand] || vNamesI[vchAcceptRand] < 0))
				continue;
			vNamesI[vchAcceptRand] = theOfferAccept.nHeight;
			string offer = stringFromVch(vchOffer);
			string sHeight = strprintf("%llu", theOfferAccept.nHeight);
			oOfferAccept.push_back(Pair("offer", offer));
			oOfferAccept.push_back(Pair("title", stringFromVch(theOffer.sTitle)));
			oOfferAccept.push_back(Pair("id", stringFromVch(vchAcceptRand)));
			oOfferAccept.push_back(Pair("btctxid", theOfferAccept.txBTCId.GetHex()));
			oOfferAccept.push_back(Pair("linkofferaccept", stringFromVch(theOfferAccept.vchLinkOfferAccept)));
			oOfferAccept.push_back(Pair("linkoffer", stringFromVch(theOfferAccept.vchLinkOffer)));
			std::vector<unsigned char> vchSellerKeyByte;
			boost::algorithm::unhex(theOffer.vchPubKey.begin(), theOffer.vchPubKey.end(), std::back_inserter(vchSellerKeyByte));
			CPubKey SellerPubKey(vchSellerKeyByte);
			CSyscoinAddress selleraddy(SellerPubKey.GetID());
			selleraddy = CSyscoinAddress(selleraddy.ToString());
			if(!selleraddy.IsValid() || !selleraddy.isAlias)
				continue;
			oOfferAccept.push_back(Pair("alias", selleraddy.aliasName));
			oOfferAccept.push_back(Pair("buyerkey", stringFromVch(theOfferAccept.vchBuyerKey)));
			oOfferAccept.push_back(Pair("height", sHeight));
			oOfferAccept.push_back(Pair("quantity", strprintf("%u", theOfferAccept.nQty)));
			oOfferAccept.push_back(Pair("currency", stringFromVch(theOffer.sCurrencyCode)));
			COfferLinkWhitelistEntry entry;
			vector<unsigned char> vchEscrowLink;
			
			if(IsSyscoinTxMine(offerTx)) 
			{
				vector<unsigned char> vchOfferLink;
				bool foundOffer = false;
				bool foundEscrow = false;
				for (unsigned int i = 0; i < offerTx.vin.size(); i++) {
					vector<vector<unsigned char> > vvchIn;
					int opIn;
					const COutPoint *prevOutput = &offerTx.vin[i].prevout;
					GetPreviousInput(prevOutput, opIn, vvchIn);
					if(foundOffer && foundEscrow)
						break;

					if (!foundOffer && IsOfferOp(opIn)) {
						foundOffer = true; 
						vchOfferLink = vvchIn[0];
					}
					if (!foundEscrow && IsEscrowOp(opIn)) {
						foundEscrow = true; 
						vchEscrowLink = vvchIn[0];
					}
				}
				if(foundOffer)
					theOffer.linkWhitelist.GetLinkEntryByHash(vchOfferLink, entry);
			}
			oOfferAccept.push_back(Pair("offer_discount_percentage", strprintf("%d%%", entry.nDiscountPct)));
			oOfferAccept.push_back(Pair("escrowlink", stringFromVch(vchEscrowLink)));
			int precision = 2;
			CAmount nPricePerUnit = convertCurrencyCodeToSyscoin(theOffer.sCurrencyCode, theOfferAccept.nPrice, theOfferAccept.nHeight, precision);
			oOfferAccept.push_back(Pair("systotal", ValueFromAmount(nPricePerUnit * theOfferAccept.nQty)));
			oOfferAccept.push_back(Pair("price", strprintf("%.*f", precision, theOfferAccept.nPrice ))); 
			oOfferAccept.push_back(Pair("total", strprintf("%.*f", precision, theOfferAccept.nPrice * theOfferAccept.nQty ))); 
			// this accept is for me(something ive sold) if this offer is mine
			oOfferAccept.push_back(Pair("ismine", IsSyscoinTxMine(offerTx)? "true" : "false"));
			if(theOfferAccept.bPaid && !theOfferAccept.bRefunded) {
				if(!theOfferAccept.txBTCId.IsNull())
					oOfferAccept.push_back(Pair("status","check payment"));
				else
					oOfferAccept.push_back(Pair("status","paid"));
			}
			else if(theOfferAccept.bRefunded) { 
				oOfferAccept.push_back(Pair("status", "refunded"));
			}
			else 
			{
				oOfferAccept.push_back(Pair("status","not paid"));
			}
			
			string strMessage = string("");
			if(!DecryptMessage(theOffer.vchPubKey, theOfferAccept.vchMessage, strMessage))
				strMessage = string("Encrypted for owner of offer");
			oOfferAccept.push_back(Pair("pay_message", strMessage));
			oRes.push_back(oOfferAccept);	
        }
		vNamesI.clear();
       BOOST_FOREACH(PAIRTYPE(const uint256, CWalletTx)& item, pwalletMain->mapWallet)
        {
			UniValue oOfferAccept(UniValue::VOBJ);
            if (item.second.nVersion != SYSCOIN_TX_VERSION)
                continue;

            // decode txn
            vector<vector<unsigned char> > vvch;
            int op, nOut;
            if (!DecodeEscrowTx(item.second, op, nOut, vvch) 
            	|| !IsEscrowOp(op))
                continue;
			
            vchEscrow = vvch[0];
		
			COfferAccept theOfferAccept;		

			CTransaction escrowTx;
			CEscrow theEscrow;
			COffer theOffer;
			CTransaction offerTx, acceptTx;
			
			if(!GetTxOfEscrow(*pescrowdb, vchEscrow, theEscrow, escrowTx))	
				continue;
			std::vector<unsigned char> vchBuyerKeyByte;
			boost::algorithm::unhex(theEscrow.vchBuyerKey.begin(), theEscrow.vchBuyerKey.end(), std::back_inserter(vchBuyerKeyByte));
			CPubKey buyerKey(vchBuyerKeyByte);
			CSyscoinAddress buyerAddress(buyerKey.GetID());
			if(!buyerAddress.IsValid() || !IsMine(*pwalletMain, buyerAddress.Get()))
				continue;
			if(!GetTxOfOffer(*pofferdb, theEscrow.vchOffer, theOffer, offerTx))	
				continue;
			if (!GetTxOfOfferAccept(*pofferdb, theEscrow.vchOffer, theEscrow.vchOfferAcceptLink, theOfferAccept, acceptTx))
				continue;

			// check for existence of offeraccept in txn offer obj
			if(theOfferAccept.vchAcceptRand != theEscrow.vchOfferAcceptLink)
				continue;	
 
            // decode txn, skip non-alias txns
            vector<vector<unsigned char> > offerVvch;
            if (!DecodeOfferTx(acceptTx, op, nOut, offerVvch) 
            	|| !IsOfferOp(op) 
            	|| (op != OP_OFFER_ACCEPT && op != OP_OFFER_REFUND))
                continue;
			if(offerVvch[0] != vchOfferToFind && !vchOfferToFind.empty())
				continue;
			const vector<unsigned char> &vchAcceptRand = offerVvch[1];
			// get last active accept only
			if (vNamesI.find(vchAcceptRand) != vNamesI.end() && (theOfferAccept.nHeight <= vNamesI[vchAcceptRand] || vNamesI[vchAcceptRand] < 0))
				continue;
			if(theOffer.IsNull())
				continue;
			vNamesI[vchAcceptRand] = theOfferAccept.nHeight;
			string offer = stringFromVch(vchOffer);
			string sHeight = strprintf("%llu", theOfferAccept.nHeight);
			oOfferAccept.push_back(Pair("offer", offer));
			oOfferAccept.push_back(Pair("title", stringFromVch(theOffer.sTitle)));
			oOfferAccept.push_back(Pair("id", stringFromVch(vchAcceptRand)));
			oOfferAccept.push_back(Pair("btctxid", theOfferAccept.txBTCId.GetHex()));
			oOfferAccept.push_back(Pair("linkofferaccept", stringFromVch(theOfferAccept.vchLinkOfferAccept)));
			oOfferAccept.push_back(Pair("linkoffer", stringFromVch(theOfferAccept.vchLinkOffer)));
			std::vector<unsigned char> vchSellerKeyByte;
			boost::algorithm::unhex(theOffer.vchPubKey.begin(), theOffer.vchPubKey.end(), std::back_inserter(vchSellerKeyByte));
			CPubKey SellerPubKey(vchSellerKeyByte);
			CSyscoinAddress selleraddy(SellerPubKey.GetID());
			selleraddy = CSyscoinAddress(selleraddy.ToString());
			if(!selleraddy.IsValid() || !selleraddy.isAlias)
				continue;
			oOfferAccept.push_back(Pair("alias", selleraddy.aliasName));
			oOfferAccept.push_back(Pair("buyerkey", stringFromVch(theOfferAccept.vchBuyerKey)));
			oOfferAccept.push_back(Pair("height", sHeight));
			oOfferAccept.push_back(Pair("quantity", strprintf("%u", theOfferAccept.nQty)));
			oOfferAccept.push_back(Pair("currency", stringFromVch(theOffer.sCurrencyCode)));
			vector<unsigned char> vchEscrowLink;
			if(IsSyscoinTxMine(offerTx)) 
			{
				bool foundEscrow = false;
				for (unsigned int i = 0; i < offerTx.vin.size(); i++) {
					vector<vector<unsigned char> > vvchIn;
					int opIn;
					const COutPoint *prevOutput = &offerTx.vin[i].prevout;
					GetPreviousInput(prevOutput, opIn, vvchIn);
					if(foundEscrow)
						break;

					if (!foundEscrow && IsEscrowOp(opIn)) {
						foundEscrow = true; 
						vchEscrowLink = vvchIn[0];
					}
				}
			}
			oOfferAccept.push_back(Pair("escrowlink", stringFromVch(vchEscrowLink)));
			int precision = 2;
			convertCurrencyCodeToSyscoin(theOffer.sCurrencyCode, 0, chainActive.Tip()->nHeight, precision);
			oOfferAccept.push_back(Pair("price", strprintf("%.*f", precision, theOfferAccept.nPrice ))); 
			oOfferAccept.push_back(Pair("total", strprintf("%.*f", precision, theOfferAccept.nPrice * theOfferAccept.nQty ))); 
			// this accept is for me(something ive sold) if this offer is mine
			oOfferAccept.push_back(Pair("ismine", IsSyscoinTxMine(offerTx)? "true" : "false"));
			if(theOfferAccept.bPaid && !theOfferAccept.bRefunded) {
				if(!theOfferAccept.txBTCId.IsNull())
					oOfferAccept.push_back(Pair("status","check"));
				else
					oOfferAccept.push_back(Pair("status","paid"));
			}
			else if(theOfferAccept.bRefunded) { 
				oOfferAccept.push_back(Pair("status", "refunded"));
			}
			else
			{
				oOfferAccept.push_back(Pair("status","not paid"));
			}

			string strMessage = string("");
			if(!DecryptMessage(theOffer.vchPubKey, theOfferAccept.vchMessage, strMessage))
				strMessage = string("Encrypted for owner of offer");
			oOfferAccept.push_back(Pair("pay_message", strMessage));
			oRes.push_back(oOfferAccept);	
        }
    }

    return oRes;
}
UniValue offerlist(const UniValue& params, bool fHelp) {
    if (fHelp || 1 < params.size())
		throw runtime_error("offerlist [offer]\n"
				"list my own offers");

    vector<unsigned char> vchName;

    if (params.size() == 1)
        vchName = vchFromValue(params[0]);

    vector<unsigned char> vchNameUniq;
    if (params.size() == 1)
        vchNameUniq = vchFromValue(params[0]);

    UniValue oRes(UniValue::VARR);
    map< vector<unsigned char>, int > vNamesI;
    map< vector<unsigned char>, UniValue > vNamesO;

    {

        uint256 blockHash;
        uint256 hash;
        CTransaction tx;
		int expired;
		int pending;
		int expires_in;
		int expired_block;
        uint64_t nHeight;

        BOOST_FOREACH(PAIRTYPE(const uint256, CWalletTx)& item, pwalletMain->mapWallet)
        {
			expired = 0;
			pending = 0;
			expires_in = 0;
			expired_block = 0;
            // get txn hash, read txn index
            hash = item.second.GetHash();

 			const CWalletTx &wtx = item.second;

            // skip non-syscoin txns
            if (wtx.nVersion != SYSCOIN_TX_VERSION)
                continue;

            // decode txn, skip non-alias txns
            vector<vector<unsigned char> > vvch;
            int op, nOut;
            if (!DecodeOfferTx(wtx, op, nOut, vvch) 
            	|| !IsOfferOp(op) 
            	|| (op == OP_OFFER_ACCEPT))
                continue;

            // get the txn name
            vchName = vvch[0];

			// skip this offer if it doesn't match the given filter value
			if (vchNameUniq.size() > 0 && vchNameUniq != vchName)
				continue;
	
			vector<COffer> vtxPos;
			COffer theOfferA;
			if (!pofferdb->ReadOffer(vchName, vtxPos))
			{
				pending = 1;
				theOfferA = COffer(tx);
			}
			if (vtxPos.size() < 1)
			{
				pending = 1;
				theOfferA = COffer(tx);
			}	
			if(pending != 1)
			{
				theOfferA = vtxPos.back();
			}
			// get last active name only
			if (vNamesI.find(vchName) != vNamesI.end() && (theOfferA.nHeight < vNamesI[vchName] || vNamesI[vchName] < 0))
				continue;	
			uint256 blockHash;
			if (!GetSyscoinTransaction(theOfferA.nHeight, theOfferA.txHash, tx, Params().GetConsensus()))
				continue;
			nHeight = theOfferA.nHeight;
            // build the output UniValue
            UniValue oName(UniValue::VOBJ);
            oName.push_back(Pair("offer", stringFromVch(vchName)));
			oName.push_back(Pair("cert", stringFromVch(theOfferA.vchCert)));
            oName.push_back(Pair("title", stringFromVch(theOfferA.sTitle)));
            oName.push_back(Pair("category", stringFromVch(theOfferA.sCategory)));
            oName.push_back(Pair("description", stringFromVch(theOfferA.sDescription)));
			int precision = 2;
			convertCurrencyCodeToSyscoin(theOfferA.sCurrencyCode, 0, chainActive.Tip()->nHeight, precision);
			oName.push_back(Pair("price", strprintf("%.*f", precision, theOfferA.GetPrice() ))); 	

			oName.push_back(Pair("currency", stringFromVch(theOfferA.sCurrencyCode) ) );
			oName.push_back(Pair("commission", strprintf("%d%%", theOfferA.nCommission)));
            oName.push_back(Pair("quantity", strprintf("%u", theOfferA.nQty)));
			std::vector<unsigned char> vchSellerKeyByte;
			boost::algorithm::unhex(theOfferA.vchPubKey.begin(), theOfferA.vchPubKey.end(), std::back_inserter(vchSellerKeyByte));
			CPubKey SellerPubKey(vchSellerKeyByte);
			CSyscoinAddress selleraddy(SellerPubKey.GetID());
			selleraddy = CSyscoinAddress(selleraddy.ToString());
			if(!selleraddy.IsValid() || !selleraddy.isAlias)
				continue;
			oName.push_back(Pair("address", selleraddy.ToString()));
			oName.push_back(Pair("exclusive_resell", theOfferA.linkWhitelist.bExclusiveResell ? "ON" : "OFF"));
			oName.push_back(Pair("btconly", theOfferA.bOnlyAcceptBTC ? "Yes" : "No"));
			oName.push_back(Pair("private", theOfferA.bPrivate ? "Yes" : "No"));
			expired_block = nHeight + GetOfferExpirationDepth();
            if(pending == 0 && (nHeight + GetOfferExpirationDepth() - chainActive.Tip()->nHeight <= 0))
			{
				expired = 1;
			}  
			if(pending == 0 && expired == 0)
			{
				expires_in = nHeight + GetOfferExpirationDepth() - chainActive.Tip()->nHeight;
			}
			oName.push_back(Pair("alias", selleraddy.aliasName));
			oName.push_back(Pair("expires_in", expires_in));
			oName.push_back(Pair("expires_on", expired_block));
			oName.push_back(Pair("expired", expired));

			oName.push_back(Pair("pending", pending));

            vNamesI[vchName] = nHeight;
            vNamesO[vchName] = oName;
        }
    }

    BOOST_FOREACH(const PAIRTYPE(vector<unsigned char>, UniValue)& item, vNamesO)
        oRes.push_back(item.second);

    return oRes;
}


UniValue offerhistory(const UniValue& params, bool fHelp) {
	if (fHelp || 1 != params.size())
		throw runtime_error("offerhistory <offer>\n"
				"List all stored values of an offer.\n");

	UniValue oRes(UniValue::VARR);
	vector<unsigned char> vchOffer = vchFromValue(params[0]);
	string offer = stringFromVch(vchOffer);

	{

		//vector<CDiskTxPos> vtxPos;
		vector<COffer> vtxPos;
		//COfferDB dbOffer("r");
		if (!pofferdb->ReadOffer(vchOffer, vtxPos) || vtxPos.empty())
			throw runtime_error("failed to read from offer DB");

		COffer txPos2;
		uint256 txHash;
		BOOST_FOREACH(txPos2, vtxPos) {
			txHash = txPos2.txHash;
			CTransaction tx;
			if (!GetSyscoinTransaction(txPos2.nHeight, txHash, tx, Params().GetConsensus())) {
				error("could not read txpos");
				continue;
			}
            // decode txn, skip non-alias txns
            vector<vector<unsigned char> > vvch;
            int op, nOut;
            if (!DecodeOfferTx(tx, op, nOut, vvch) 
            	|| !IsOfferOp(op) )
                continue;

			int expired = 0;
			int expires_in = 0;
			int expired_block = 0;
			UniValue oOffer(UniValue::VOBJ);
			vector<unsigned char> vchValue;
			uint64_t nHeight;
			nHeight = txPos2.nHeight;
			COffer theOfferA = txPos2;
			oOffer.push_back(Pair("offer", offer));
			string opName = offerFromOp(op);
			oOffer.push_back(Pair("offertype", opName));
			oOffer.push_back(Pair("cert", stringFromVch(theOfferA.vchCert)));
            oOffer.push_back(Pair("title", stringFromVch(theOfferA.sTitle)));
            oOffer.push_back(Pair("category", stringFromVch(theOfferA.sCategory)));
            oOffer.push_back(Pair("description", stringFromVch(theOfferA.sDescription)));
			int precision = 2;
			convertCurrencyCodeToSyscoin(theOfferA.sCurrencyCode, 0, chainActive.Tip()->nHeight, precision);
			oOffer.push_back(Pair("price", strprintf("%.*f", precision, theOfferA.GetPrice() ))); 	

			oOffer.push_back(Pair("currency", stringFromVch(theOfferA.sCurrencyCode) ) );
			oOffer.push_back(Pair("commission", strprintf("%d%%", theOfferA.nCommission)));
            oOffer.push_back(Pair("quantity", strprintf("%u", theOfferA.nQty)));

			oOffer.push_back(Pair("txid", tx.GetHash().GetHex()));
			expired_block = nHeight + GetOfferExpirationDepth();
			if(nHeight + GetOfferExpirationDepth() - chainActive.Tip()->nHeight <= 0)
			{
				expired = 1;
			}  
			if(expired == 0)
			{
				expires_in = nHeight + GetOfferExpirationDepth() - chainActive.Tip()->nHeight;
			}
			std::vector<unsigned char> vchSellerKeyByte;
			boost::algorithm::unhex(txPos2.vchPubKey.begin(), txPos2.vchPubKey.end(), std::back_inserter(vchSellerKeyByte));
			CPubKey SellerPubKey(vchSellerKeyByte);
			CSyscoinAddress selleraddy(SellerPubKey.GetID());
			selleraddy = CSyscoinAddress(selleraddy.ToString());
			if(!selleraddy.IsValid() || !selleraddy.isAlias)
				continue;
			oOffer.push_back(Pair("alias", selleraddy.aliasName));
			oOffer.push_back(Pair("expires_in", expires_in));
			oOffer.push_back(Pair("expires_on", expired_block));
			oOffer.push_back(Pair("expired", expired));
			oOffer.push_back(Pair("height", strprintf("%d", theOfferA.nHeight)));
			oRes.push_back(oOffer);
		}
	}
	return oRes;
}

UniValue offerfilter(const UniValue& params, bool fHelp) {
	if (fHelp || params.size() > 5)
		throw runtime_error(
				"offerfilter [[[[[regexp] maxage=36000] from=0] nb=0] stat]\n"
						"scan and filter offeres\n"
						"[regexp] : apply [regexp] on offeres, empty means all offeres\n"
						"[maxage] : look in last [maxage] blocks\n"
						"[from] : show results from number [from]\n"
						"[nb] : show [nb] results, 0 means all\n"
						"[stats] : show some stats instead of results\n"
						"offerfilter \"\" 5 # list offeres updated in last 5 blocks\n"
						"offerfilter \"^offer\" # list all offeres starting with \"offer\"\n"
						"offerfilter 36000 0 0 stat # display stats (number of offers) on active offeres\n");

	string strRegexp;
	int nFrom = 0;
	int nNb = 0;
	int nMaxAge = GetOfferExpirationDepth();
	bool fStat = false;
	int nCountFrom = 0;
	int nCountNb = 0;

	if (params.size() > 0)
		strRegexp = params[0].get_str();

	if (params.size() > 1)
		nMaxAge = params[1].get_int();

	if (params.size() > 2)
		nFrom = params[2].get_int();

	if (params.size() > 3)
		nNb = params[3].get_int();

	if (params.size() > 4)
		fStat = (params[4].get_str() == "stat" ? true : false);

	//COfferDB dbOffer("r");
	UniValue oRes(UniValue::VARR);

	vector<unsigned char> vchOffer;
	vector<pair<vector<unsigned char>, COffer> > offerScan;
	if (!pofferdb->ScanOffers(vchOffer, 100000000, offerScan))
		throw runtime_error("scan failed");

    // regexp
    using namespace boost::xpressive;
    smatch offerparts;
    sregex cregex = sregex::compile(strRegexp);

	pair<vector<unsigned char>, COffer> pairScan;
	BOOST_FOREACH(pairScan, offerScan) {
		COffer txOffer = pairScan.second;
		string offer = stringFromVch(pairScan.first);
		string title = stringFromVch(txOffer.sTitle);
		std::vector<unsigned char> vchSellerKeyByte;
		boost::algorithm::unhex(txOffer.vchPubKey.begin(), txOffer.vchPubKey.end(), std::back_inserter(vchSellerKeyByte));
		CPubKey SellerPubKey(vchSellerKeyByte);
		CSyscoinAddress selleraddy(SellerPubKey.GetID());
		selleraddy = CSyscoinAddress(selleraddy.ToString());
		if(!selleraddy.IsValid() || !selleraddy.isAlias)
			continue;
		string alias = selleraddy.aliasName;
        if (strRegexp != "" && !regex_search(title, offerparts, cregex) && strRegexp != offer && strRegexp != alias)
            continue;

		int expired = 0;
		int expires_in = 0;
		int expired_block = 0;		
		int nHeight = txOffer.nHeight;

		// max age
		if (nMaxAge != 0 && chainActive.Tip()->nHeight - nHeight >= nMaxAge)
			continue;

		// from limits
		nCountFrom++;
		if (nCountFrom < nFrom + 1)
			continue;
        CTransaction tx;
		if (!GetSyscoinTransaction(txOffer.nHeight, txOffer.txHash, tx, Params().GetConsensus()))
			continue;
		// dont return sold out offers
		if(txOffer.nQty <= 0)
			continue;
		if(txOffer.bPrivate)
			continue;
		UniValue oOffer(UniValue::VOBJ);
		oOffer.push_back(Pair("offer", offer));
		oOffer.push_back(Pair("cert", stringFromVch(txOffer.vchCert)));
        oOffer.push_back(Pair("title", stringFromVch(txOffer.sTitle)));
		oOffer.push_back(Pair("description", stringFromVch(txOffer.sDescription)));
        oOffer.push_back(Pair("category", stringFromVch(txOffer.sCategory)));
		int precision = 2;
		convertCurrencyCodeToSyscoin(txOffer.sCurrencyCode, 0, chainActive.Tip()->nHeight, precision);
		oOffer.push_back(Pair("price", strprintf("%.*f", precision, txOffer.GetPrice() ))); 	
		oOffer.push_back(Pair("currency", stringFromVch(txOffer.sCurrencyCode)));
		oOffer.push_back(Pair("commission", strprintf("%d%%", txOffer.nCommission)));
        oOffer.push_back(Pair("quantity", strprintf("%u", txOffer.nQty)));
		oOffer.push_back(Pair("exclusive_resell", txOffer.linkWhitelist.bExclusiveResell ? "ON" : "OFF"));
		oOffer.push_back(Pair("btconly", txOffer.bOnlyAcceptBTC ? "Yes" : "No"));
		expired_block = nHeight + GetOfferExpirationDepth();
		if(nHeight + GetOfferExpirationDepth() - chainActive.Tip()->nHeight <= 0)
		{
			expired = 1;
		}  
		if(expired == 0)
		{
			expires_in = nHeight + GetOfferExpirationDepth() - chainActive.Tip()->nHeight;
		}
		std::vector<unsigned char> vchSellerKeyByte;
		boost::algorithm::unhex(txOffer.vchPubKey.begin(), txOffer.vchPubKey.end(), std::back_inserter(vchSellerKeyByte));
		CPubKey SellerPubKey(vchSellerKeyByte);
		CSyscoinAddress selleraddy(SellerPubKey.GetID());
		selleraddy = CSyscoinAddress(selleraddy.ToString());
		if(!selleraddy.IsValid() || !selleraddy.isAlias)
			continue;
		oOffer.push_back(Pair("private", txOffer.bPrivate ? "Yes" : "No"));
		oOffer.push_back(Pair("alias", selleraddy.aliasName));
		oOffer.push_back(Pair("expires_in", expires_in));
		oOffer.push_back(Pair("expires_on", expired_block));
		oOffer.push_back(Pair("expired", expired));
		oRes.push_back(oOffer);

		nCountNb++;
		// nb limits
		if (nNb > 0 && nCountNb >= nNb)
			break;
	}

	if (fStat) {
		UniValue oStat(UniValue::VOBJ);
		oStat.push_back(Pair("blocks", (int) chainActive.Tip()->nHeight));
		oStat.push_back(Pair("count", (int) oRes.size()));
		//oStat.push_back(Pair("sha256sum", SHA256(oRes), true));
		return oStat;
	}

	return oRes;
}

UniValue offerscan(const UniValue& params, bool fHelp) {
	if (fHelp || 2 > params.size())
		throw runtime_error(
				"offerscan [<start-offer>] [<max-returned>]\n"
						"scan all offers, starting at start-offer and returning a maximum number of entries (default 500)\n");

	vector<unsigned char> vchOffer;
	int nMax = 500;
	if (params.size() > 0) {
		vchOffer = vchFromValue(params[0]);
	}

	if (params.size() > 1) {
		nMax = params[1].get_int();
	}

	//COfferDB dbOffer("r");
	UniValue oRes(UniValue::VARR);

	vector<pair<vector<unsigned char>, COffer> > offerScan;
	if (!pofferdb->ScanOffers(vchOffer, nMax, offerScan))
		throw runtime_error("scan failed");

	pair<vector<unsigned char>, COffer> pairScan;
	BOOST_FOREACH(pairScan, offerScan) {
		int expired = 0;
		int expires_in = 0;
		int expired_block = 0;
		UniValue oOffer(UniValue::VOBJ);
		string offer = stringFromVch(pairScan.first);
		oOffer.push_back(Pair("offer", offer));
		CTransaction tx;
		COffer txOffer = pairScan.second;
		// dont return sold out offers
		if(txOffer.nQty <= 0)
			continue;
		if(txOffer.bPrivate)
			continue;
		uint256 blockHash;

		int nHeight = txOffer.nHeight;
		expired_block = nHeight + GetOfferExpirationDepth();
		if(nHeight + GetOfferExpirationDepth() - chainActive.Tip()->nHeight <= 0)
		{
			expired = 1;
		}  
		if(expired == 0)
		{
			expires_in = nHeight + GetOfferExpirationDepth() - chainActive.Tip()->nHeight;
		}
		std::vector<unsigned char> vchSellerKeyByte;
		boost::algorithm::unhex(txOffer.vchPubKey.begin(), txOffer.vchPubKey.end(), std::back_inserter(vchSellerKeyByte));
		CPubKey SellerPubKey(vchSellerKeyByte);
		CSyscoinAddress selleraddy(SellerPubKey.GetID());
		selleraddy = CSyscoinAddress(selleraddy.ToString());
		if(!selleraddy.IsValid() || !selleraddy.isAlias)
			continue;
		oOffer.push_back(Pair("alias", selleraddy.aliasName));
		oOffer.push_back(Pair("expires_in", expires_in));
		oOffer.push_back(Pair("expires_on", expired_block));
		oOffer.push_back(Pair("expired", expired));
		oRes.push_back(oOffer);
	}

	return oRes;
}
void PutOfferAccept(std::vector<COffer> &offerList, COffer& theOffer, const COfferAccept &theOA){
	if(offerList.empty())
		return;
    for(int i=offerList.size()-1;i>=0;i--) {
		if(offerList[i].accept.IsNull())
			continue;
        if(offerList[i].accept.vchAcceptRand == theOA.vchAcceptRand) {
            offerList[i].accept = theOA;
			return;
        }
    }
	theOffer.accept = theOA;
}
bool GetAcceptByHash(const std::vector<COffer> &offerList, COfferAccept &ca) {
	if(offerList.empty())
		return false;
    for(int i=offerList.size()-1;i>=0;i--) {
		if(offerList[i].accept.IsNull())
			continue;
        if(offerList[i].accept.vchAcceptRand == ca.vchAcceptRand) {
            ca = offerList[i].accept;
			return true;
        }
    }
    ca = offerList.back().accept;
	return false;
}