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
#include "chainparams.h"
#include <boost/algorithm/hex.hpp>
#include <boost/xpressive/xpressive_dynamic.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/foreach.hpp>
#include <boost/thread.hpp>
using namespace std;
extern void SendMoney(const CTxDestination &address, CAmount nValue, bool fSubtractFeeFromAmount, CWalletTx& wtxNew);
extern void SendMoneySyscoin(const vector<CRecipient> &vecSend, CAmount nValue, bool fSubtractFeeFromAmount, CWalletTx& wtxNew, const CWalletTx* wtxIn=NULL, bool syscoinTx=true);
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
		if (DecodeOfferTx(wtx, op, nOut, vvchArgs, -1))
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
		if (DecodeOfferTx(wtx, op, nOut, vvchArgs, -1))
		{
			if(op == OP_OFFER_ACCEPT)
			{
				if(vvchArgs.size() == 2 && vvchArgs[0] == vchOffer)
				{
					COffer theOffer(wtx);
					COfferAccept theOfferAccept;
					if (theOffer.IsNull())
						continue;

					if(theOffer.GetAcceptByHash(vvchArgs[1], theOfferAccept))
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
	params.push_back("1");	
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
string makeOfferLinkAcceptTX(const COfferAccept& theOfferAccept, const vector<unsigned char> &vchPubKey, const vector<unsigned char> &vchOffer, const vector<unsigned char> &vchOfferAcceptLink)
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

	int64_t heightToCheckAgainst = theOfferAccept.nHeight;
	
	// if we want to accept an escrow release or we are accepting a linked offer from an escrow release. Override heightToCheckAgainst if using escrow since escrow can take a long time.
	if(!theOfferAccept.vchEscrowLink.empty())
	{
		// get escrow activation UniValue
		vector<CEscrow> escrowVtxPos;
		if (pescrowdb->ExistsEscrow(theOfferAccept.vchEscrowLink)) {
			if (pescrowdb->ReadEscrow(theOfferAccept.vchEscrowLink, escrowVtxPos))
			{	
				// we want the initial funding escrow transaction height as when to calculate this offer accept price from convertCurrencyCodeToSyscoin()
				CEscrow fundingEscrow = escrowVtxPos.front();
				heightToCheckAgainst = fundingEscrow.nHeight;
			}
		}

	}

	pwalletMain->GetKeyFromPool(newDefaultKey);
	CSyscoinAddress refundAddr = CSyscoinAddress(newDefaultKey.GetID());
	const vector<unsigned char> vchRefundAddress = vchFromString(refundAddr.ToString());
	params.push_back(stringFromVch(vchOffer));
	params.push_back(static_cast<ostringstream*>( &(ostringstream() << theOfferAccept.nQty) )->str());
	params.push_back(stringFromVch(vchPubKey));
	params.push_back(stringFromVch(theOfferAccept.vchMessage));
	params.push_back(stringFromVch(vchRefundAddress));
	params.push_back(stringFromVch(vchOfferAcceptLink));
	params.push_back("");
	params.push_back(static_cast<ostringstream*>( &(ostringstream() << heightToCheckAgainst) )->str());
	
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
string makeOfferRefundTX(const CTransaction& prevTx, const vector<unsigned char> &vchAcceptRand, const vector<unsigned char> &refundCode)
{
	CTransaction myPrevTx = prevTx;
	CTransaction myOfferTx;
	COffer theOffer;
	COfferAccept theOfferAccept;
	if(GetTxOfOfferAccept(*pofferdb, vchAcceptRand, theOffer, myOfferTx))
	{
		if(!IsOfferMine(myOfferTx))
			return "";
		if(theOfferAccept.nQty <= 0)
			return string("makeOfferRefundTX(): cannot refund an accept with 0 quantity");

	}
	else
		return "";
	if(myPrevTx.IsNull())
		myPrevTx = myOfferTx;
	if(!pwalletMain)
	{
		return string("makeOfferRefundTX(): no wallet found");
	}
	if(theOfferAccept.bRefunded)
	{
		return string("This offer accept has already been refunded");
	}	
	const CWalletTx *wtxPrevIn;
	wtxPrevIn = pwalletMain->GetWalletTx(myPrevTx.GetHash());
	if (wtxPrevIn == NULL)
	{
		return string("makeOfferRefundTX() : can't find this offer in your wallet");
	}

    vector<vector<unsigned char> > vvch;
    int op, nOut;
    // decode the offer op, params, height
    if(!DecodeOfferTx(myOfferTx, op, nOut, vvch, -1))
	{
		return string("makeOfferRefundTX(): could not decode offer transaction");
	}
	const vector<unsigned char> &vchOffer = vvch[0];

	// this is a syscoin txn
	CWalletTx wtx, wtx2;
	int64_t nTotalValue = 0;
	CScript scriptPubKeyOrig, scriptPayment;

	if(refundCode == OFFER_REFUND_COMPLETE)
	{
		int precision = 2;
		COfferLinkWhitelistEntry entry;
		theOffer.linkWhitelist.GetLinkEntryByHash(theOfferAccept.vchCertLink, entry);
		// lookup the price of the offer in syscoin based on pegged alias at the block # when accept was made (sets nHeight in offeraccept serialized UniValue in tx)
		nTotalValue = convertCurrencyCodeToSyscoin(theOffer.sCurrencyCode, theOffer.GetPrice(entry), theOfferAccept.nHeight, precision)*theOfferAccept.nQty;
	} 


	// create OFFERACCEPT txn keys
	CScript scriptPubKey;
    CPubKey newDefaultKey;
    pwalletMain->GetKeyFromPool(newDefaultKey);
	scriptPubKeyOrig= GetScriptForDestination(newDefaultKey.GetID());

	scriptPubKey << CScript::EncodeOP_N(OP_OFFER_REFUND) << vchOffer << vchAcceptRand << refundCode << OP_2DROP << OP_2DROP;
	scriptPubKey += scriptPubKeyOrig;
	
	if (ExistsInMempool(vchOffer, OP_OFFER_ACTIVATE) || ExistsInMempool(vchOffer, OP_OFFER_UPDATE)) {
		return string("there are pending operations or refunds on that offer");
	}

	if(foundRefundInWallet(vchAcceptRand, refundCode))
	{
		return string("foundRefundInWallet - This offer accept has already been refunded");
	}
    // add a copy of the offer with just
    // the one accept to save bandwidth
    COffer offerCopy = theOffer;
    COfferAccept offerAcceptCopy = theOfferAccept;
    offerCopy.ClearOffer();
    offerCopy.PutOfferAccept(offerAcceptCopy);

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
	SendMoneySyscoin(vecSend, recipient.nAmount+fee.nAmount, false, wtx, wtxPrevIn);
	
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
    CDBIterator *pcursor = pofferdb->NewIterator();

    CDataStream ssKeySet(SER_DISK, CLIENT_VERSION);
    ssKeySet << make_pair(string("offeri"), vchOffer);
    pcursor->Seek(ssKeySet.str());

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
    delete pcursor;
    return true;
}

/**
 * [COfferDB::ReconstructOfferIndex description]
 * @param  pindexRescan [description]
 * @return              [description]
 */
bool COfferDB::ReconstructOfferIndex(CBlockIndex *pindexRescan) {
    CBlockIndex* pindex = pindexRescan;  
	if(!HasReachedMainNetForkB2())
		return true;
    {
	TRY_LOCK(pwalletMain->cs_wallet, cs_trylock);
    while (pindex) {  

        int nHeight = pindex->nHeight;
        CBlock block;
        ReadBlockFromDisk(block, pindex, Params().GetConsensus());
        uint256 txblkhash;
        
        BOOST_FOREACH(CTransaction& tx, block.vtx) {
            if (tx.nVersion != SYSCOIN_TX_VERSION)
                continue;

            vector<vector<unsigned char> > vvchArgs;
            int op, nOut;

            // decode the offer op, params, height
            bool o = DecodeOfferTx(tx, op, nOut, vvchArgs, -1);
            if (!o || !IsOfferOp(op)) continue;         
            vector<unsigned char> vchOffer = vvchArgs[0];
        
            // get the transaction
            if(!GetTransaction(tx.GetHash(), tx, Params().GetConsensus(), txblkhash, true))
                continue;

            // attempt to read offer from txn
            COffer txOffer;
            COfferAccept txCA;
            if(!txOffer.UnserializeFromTx(tx))
				return error("ReconstructOfferIndex() : failed to unserialize offer from tx");

			// save serialized offer
			COffer serializedOffer = txOffer;

            // read offer from DB if it exists
            vector<COffer> vtxPos;
            if (ExistsOffer(vchOffer)) {
                if (!ReadOffer(vchOffer, vtxPos))
                    return error("ReconstructOfferIndex() : failed to read offer from DB");
                if(vtxPos.size()!=0) {
                	txOffer.nHeight = nHeight;
                	txOffer.GetOfferFromList(vtxPos);
                }
            }
			// use the txn offer as master on updates,
			// but grab the accepts from the DB first
			if(op == OP_OFFER_UPDATE || op == OP_OFFER_REFUND) {
				serializedOffer.accepts = txOffer.accepts;
				txOffer = serializedOffer;
			}

			if(op == OP_OFFER_REFUND)
			{
            	vector<unsigned char> vchOfferAccept = vvchArgs[1];
	            if (ExistsOfferAccept(vchOfferAccept)) {
	                if (!ReadOfferAccept(vchOfferAccept, vchOffer))
	                    return error("ReconstructOfferIndex() : failed to read offer accept from offer DB\n");
	            }
				if(!txOffer.GetAcceptByHash(vchOfferAccept, txCA))
					 return error("ReconstructOfferIndex() OP_OFFER_REFUND: failed to read offer accept from serializedOffer\n");

				if(vvchArgs[2] == OFFER_REFUND_COMPLETE){
					txCA.bRefunded = true;
					txCA.txRefundId = tx.GetHash();
				}	
				txCA.bPaid = true;
		        txCA.txHash = tx.GetHash();
				txOffer.PutOfferAccept(txCA);
				
			}
            // read the offer accept from db if exists
            if(op == OP_OFFER_ACCEPT) {
            	bool bReadOffer = false;
            	vector<unsigned char> vchOfferAccept = vvchArgs[1];
	            if (ExistsOfferAccept(vchOfferAccept)) {
	                if (!ReadOfferAccept(vchOfferAccept, vchOffer))
	                    LogPrintf("ReconstructOfferIndex() : warning - failed to read offer accept from offer DB\n");
	                else bReadOffer = true;
	            }
				if(!bReadOffer && !txOffer.GetAcceptByHash(vchOfferAccept, txCA))
					 return error("ReconstructOfferIndex() OP_OFFER_ACCEPT: failed to read offer accept from offer\n");

				// add txn-specific values to offer accept UniValue
				txCA.bPaid = true;
				txCA.vchAcceptRand = vchOfferAccept;
		        txCA.txHash = tx.GetHash();
				txOffer.PutOfferAccept(txCA);
			}


			if(op == OP_OFFER_ACTIVATE || op == OP_OFFER_UPDATE || op == OP_OFFER_REFUND) {
				txOffer.txHash = tx.GetHash();
	            txOffer.nHeight = nHeight;
			}


            txOffer.PutToOfferList(vtxPos);

            if (!WriteOffer(vchOffer, vtxPos))
                return error("ReconstructOfferIndex() : failed to write to offer DB");
            if(op == OP_OFFER_ACCEPT || op == OP_OFFER_REFUND)
	            if (!WriteOfferAccept(vvchArgs[1], vvchArgs[0]))
	                return error("ReconstructOfferIndex() : failed to write to offer DB");
			
			if(fDebug)
				LogPrintf( "RECONSTRUCT OFFER: op=%s offer=%s title=%s qty=%u hash=%s height=%d\n",
					offerFromOp(op).c_str(),
					stringFromVch(vvchArgs[0]).c_str(),
					stringFromVch(txOffer.sTitle).c_str(),
					txOffer.nQty,
					tx.GetHash().ToString().c_str(), 
					nHeight);
			
        }
        pindex = chainActive.Next(pindex);
    }
    }
    return true;
}

int IndexOfOfferOutput(const CTransaction& tx) {
	vector<vector<unsigned char> > vvch;
	int op, nOut;
	if (!DecodeOfferTx(tx, op, nOut, vvch, -1))
		throw runtime_error("IndexOfOfferOutput() : offer output not found");
	return nOut;
}

bool IsOfferMine(const CTransaction& tx) {
	if (tx.nVersion != SYSCOIN_TX_VERSION)
		return false;

	vector<vector<unsigned char> > vvch;
	int op, nOut;

	bool good = DecodeOfferTx(tx, op, nOut, vvch, -1);
	if (!good) 
		return false;
	
	if(!IsOfferOp(op))
		return false;

	const CTxOut& txout = tx.vout[nOut];
	if (pwalletMain->IsMine(txout)) {
		return true;
	}
	return false;
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

	uint256 hashBlock;
	if (!GetTransaction(txPos.txHash, tx, Params().GetConsensus(), hashBlock, true))
		return false;

	return true;
}

bool GetTxOfOfferAccept(COfferDB& dbOffer, const vector<unsigned char> &vchOfferAccept,
		COffer &txPos, CTransaction& tx) {
	vector<COffer> vtxPos;
	vector<unsigned char> vchOffer;
	if (!pofferdb->ReadOfferAccept(vchOfferAccept, vchOffer)) return false;
	if (!pofferdb->ReadOffer(vchOffer, vtxPos) || vtxPos.empty()) return false;
	txPos = vtxPos.back();
	int nHeight = txPos.nHeight;
	if (nHeight + GetOfferExpirationDepth()
			< chainActive.Tip()->nHeight) {
		string offer = stringFromVch(vchOfferAccept);
		if(fDebug)
			LogPrintf("GetTxOfOfferAccept(%s) : expired", offer.c_str());
		return false;
	}

	uint256 hashBlock;
	if (!GetTransaction(txPos.txHash, tx, Params().GetConsensus(), hashBlock, true))
		return error("GetTxOfOfferAccept() : could not read tx from disk");

	return true;
}

bool DecodeOfferTx(const CTransaction& tx, int& op, int& nOut,
		vector<vector<unsigned char> >& vvch, int nHeight) {
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
		vector<vector<unsigned char> > &vvch) {
	CScript::const_iterator pc = script.begin();
	return DecodeOfferScript(script, op, vvch, pc);
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

bool GetOfferAddress(const CTransaction& tx, std::string& strAddress) {
	int op, nOut = 0;
	vector<vector<unsigned char> > vvch;

	if (!DecodeOfferTx(tx, op, nOut, vvch, -1))
		return error("GetOfferAddress() : could not decode offer tx.");

	const CTxOut& txout = tx.vout[nOut];

	const CScript& scriptPubKey = RemoveOfferScriptPrefix(txout.scriptPubKey);
	CTxDestination dest;
	ExtractDestination(scriptPubKey, dest);
	strAddress = CSyscoinAddress(dest).ToString();
	return true;
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
		bool fJustCheck, int nHeight) {
	if (!tx.IsCoinBase()) {
		if (fDebug)
			LogPrintf("*** %d %d %s %s %s %s\n", nHeight,
				chainActive.Tip()->nHeight, tx.GetHash().ToString().c_str(),
				fBlock ? "BLOCK" : "", fMiner ? "MINER" : "",
				fJustCheck ? "JUSTCHECK" : "");

		bool found = false;
		const COutPoint *prevOutput = NULL;
		CCoins prevCoins;
		int prevOp, prevOp1;
		vector<vector<unsigned char> > vvchPrevArgs, vvchPrevArgs1;
		vvchPrevArgs.clear();
		// Strict check - bug disallowed, find 2 inputs max
		for (unsigned int i = 0; i < tx.vin.size(); i++) {
			prevOutput = &tx.vin[i].prevout;
			inputs.GetCoins(prevOutput->hash, prevCoins);
			vector<vector<unsigned char> > vvch, vvch2, vvch3;
			if(!found)
			{
				if (DecodeOfferScript(prevCoins.vout[prevOutput->n].scriptPubKey, prevOp, vvch)) {
					found = true; 
					vvchPrevArgs = vvch;
					break;
				}
				else if (DecodeCertScript(prevCoins.vout[prevOutput->n].scriptPubKey, prevOp, vvch2))
				{
					found = true; 
					vvchPrevArgs = vvch2;
					break;
				}
			}
			else
			{
				if (DecodeOfferScript(prevCoins.vout[prevOutput->n].scriptPubKey, prevOp1, vvch)) {
					found = true; 
					vvchPrevArgs1 = vvch;
					break;
				}
				else if (DecodeCertScript(prevCoins.vout[prevOutput->n].scriptPubKey, prevOp1, vvch2))
				{
					found = true; 
					vvchPrevArgs1 = vvch2;
					break;
				}
			}
			if(!found)vvchPrevArgs.clear();
		}

		// Make sure offer outputs are not spent by a regular transaction, or the offer would be lost
		if (tx.nVersion != SYSCOIN_TX_VERSION) {
			if (found)
				return error(
						"CheckOfferInputs() : a non-syscoin transaction with a syscoin input");
			return true;
		}

		vector<vector<unsigned char> > vvchArgs;
		int op;
		int nOut;
		bool good = DecodeOfferTx(tx, op, nOut, vvchArgs, -1);
		if (!good)
			return error("CheckOfferInputs() : could not decode a syscoin tx");
		int nDepth;
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
		if(theOffer.accepts.size() > 1)
		{
			return error("offer has too many accepts, only one allowed per tx");
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

		switch (op) {
		case OP_OFFER_ACTIVATE:
			if (found && !IsCertOp(prevOp) )
				return error("offeractivate previous op is invalid");		
			if (found && vvchPrevArgs[0] != vvchArgs[0])
				return error("CheckOfferInputs() : offernew cert mismatch");
			break;


		case OP_OFFER_UPDATE:
			if ( !found || ( prevOp != OP_OFFER_ACTIVATE && prevOp != OP_OFFER_UPDATE 
				&& prevOp != OP_OFFER_REFUND ) )
				return error("offerupdate previous op %s is invalid", offerFromOp(prevOp).c_str());
			
			if (vvchPrevArgs[0] != vvchArgs[0])
				return error("CheckOfferInputs() : offerupdate offer mismatch");
			break;
		case OP_OFFER_REFUND:
			int nDepth;
			if ( !found || ( prevOp != OP_OFFER_ACTIVATE && prevOp != OP_OFFER_UPDATE && prevOp != OP_OFFER_REFUND ))
				return error("offerrefund previous op %s is invalid", offerFromOp(prevOp).c_str());		
			if(op == OP_OFFER_REFUND && vvchArgs[2] == OFFER_REFUND_COMPLETE && vvchPrevArgs[2] != OFFER_REFUND_PAYMENT_INPROGRESS)
				return error("offerrefund complete tx must be linked to an inprogress tx");
			
			if (vvchArgs[1].size() > MAX_NAME_LENGTH)
				return error("offerrefund tx with guid too big");
			if (vvchArgs[2].size() > MAX_ID_LENGTH)
				return error("offerrefund refund status too long");
			if (vvchPrevArgs[0] != vvchArgs[0])
				return error("CheckOfferInputs() : offerrefund offer mismatch");
			if (fBlock && !fJustCheck) {
				// Check hash
				const vector<unsigned char> &vchAcceptRand = vvchArgs[1];
				// check for existence of offeraccept in txn offer obj
				if(!theOffer.GetAcceptByHash(vchAcceptRand, theOfferAccept))
					return error("OP_OFFER_REFUND could not read accept from offer txn");
			}
			break;
		case OP_OFFER_ACCEPT:
			// if only cert input
			if (found && !vvchPrevArgs.empty() && !IsCertOp(prevOp))
				return error("CheckOfferInputs() : offeraccept cert/escrow input tx mismatch");
			if (vvchArgs[1].size() > MAX_NAME_LENGTH)
				return error("offeraccept tx with guid too big");
			// check for existence of offeraccept in txn offer obj
			if(!theOffer.GetAcceptByHash(vvchArgs[1], theOfferAccept))
				return error("OP_OFFER_ACCEPT could not read accept from offer txn");
			if (theOfferAccept.vchAcceptRand.size() > MAX_NAME_LENGTH)
				return error("OP_OFFER_ACCEPT offer accept hex guid too long");
			if (theOfferAccept.vchMessage.size() > MAX_ENCRYPTED_VALUE_LENGTH)
				return error("OP_OFFER_ACCEPT message field too big");
			if (theOfferAccept.vchRefundAddress.size() > MAX_NAME_LENGTH)
				return error("OP_OFFER_ACCEPT refund field too big");
			if (theOfferAccept.vchLinkOfferAccept.size() > MAX_NAME_LENGTH)
				return error("OP_OFFER_ACCEPT offer accept link field too big");
			if (theOfferAccept.vchCertLink.size() > MAX_NAME_LENGTH)
				return error("OP_OFFER_ACCEPT cert link field too big");
			if (theOfferAccept.vchEscrowLink.size() > MAX_NAME_LENGTH)
				return error("OP_OFFER_ACCEPT escrow link field too big");
			if (fBlock && !fJustCheck) {
				if(found && IsCertOp(prevOp) && theOfferAccept.vchCertLink != vvchPrevArgs[0])
				{
					return error("theOfferAccept.vchCertlink and vvchPrevArgs[0] don't match");
				}
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

			if (!fMiner && !fJustCheck && chainActive.Tip()->nHeight != nHeight) {
				int nHeight = chainActive.Tip()->nHeight;
				// get the latest offer from the db
            	theOffer.nHeight = nHeight;
            	theOffer.GetOfferFromList(vtxPos);

				// If update, we make the serialized offer the master
				// but first we assign the accepts/offerLinks/whitelists from the DB since
				// they are not shipped in an update txn to keep size down
				if(op == OP_OFFER_UPDATE) {
					const COffer& dbOffer = vtxPos.back();
					serializedOffer.accepts = theOffer.accepts;
					serializedOffer.offerLinks = theOffer.offerLinks;
					theOffer = serializedOffer;
					// whitelist must be preserved in serialOffer and db offer must have the latest in the db for whitelists
					theOffer.linkWhitelist = dbOffer.linkWhitelist;
					// some fields are only updated if they are not empty to limit txn size, rpc sends em as empty if we arent changing them
					if(serializedOffer.sCurrencyCode.empty())
						theOffer.sCurrencyCode = dbOffer.sCurrencyCode;
					if(serializedOffer.sCategory.empty())
						theOffer.sCategory = dbOffer.sCategory;
					if(serializedOffer.sTitle.empty())
						theOffer.sTitle = dbOffer.sTitle;
					if(serializedOffer.sDescription.empty())
						theOffer.sDescription = dbOffer.sDescription;
					if(serializedOffer.vchPubKey.empty())
						theOffer.vchPubKey = dbOffer.vchPubKey;
					if(serializedOffer.aliasName.empty())
						theOffer.aliasName = dbOffer.aliasName;
					if(serializedOffer.vchLinkOffer.empty())
						theOffer.vchLinkOffer = dbOffer.vchLinkOffer;

				}
				else if(op == OP_OFFER_ACTIVATE)
				{
					// if this is a linked offer activate, then add it to the parent offerLinks list
					if(!theOffer.vchLinkOffer.empty())
					{
						vector<COffer> myVtxPos;
						if (pofferdb->ExistsOffer(theOffer.vchLinkOffer)) {
							if (pofferdb->ReadOffer(theOffer.vchLinkOffer, myVtxPos))
							{
								COffer myParentOffer = myVtxPos.back();
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
					if (pofferdb->ExistsOfferAccept(vchOfferAccept)) {
						if (!pofferdb->ReadOfferAccept(vchOfferAccept, vvchArgs[0]))
						{
							return error("CheckOfferInputs()- OP_OFFER_REFUND: failed to read offer accept from offer DB\n");
						}

					}

					if(!theOffer.GetAcceptByHash(vchOfferAccept, theOfferAccept))
						return error("CheckOfferInputs()- OP_OFFER_REFUND: could not read accept from serializedOffer txn");	
            		
					if(!fInit && pwalletMain && vvchArgs[2] == OFFER_REFUND_PAYMENT_INPROGRESS){
						string strError = makeOfferRefundTX(tx, vvchArgs[1], OFFER_REFUND_COMPLETE);
						if (strError != "" && fDebug)							
							LogPrintf("CheckOfferInputs() - OFFER_REFUND_COMPLETE %s\n", strError.c_str());
						// if this accept was done via offer linking (makeOfferLinkAcceptTX) then walk back up and refund
						// special case for tmp it will get overwritten by theOfferAccept.vchLinkOfferAccept tx inside makeOfferRefundTX (used for input transaction to this refund tx)
						CTransaction tmp;
						strError = makeOfferRefundTX(tmp, theOfferAccept.vchLinkOfferAccept, OFFER_REFUND_PAYMENT_INPROGRESS);
						if (strError != "" && fDebug)							
							LogPrintf("CheckOfferInputs() - OFFER_REFUND_PAYMENT_INPROGRESS %s\n", strError.c_str());			
					}
					else if(vvchArgs[2] == OFFER_REFUND_COMPLETE){
						theOfferAccept.bRefunded = true;
						theOfferAccept.txRefundId = tx.GetHash();
					}
					theOffer.PutOfferAccept(theOfferAccept);
					{
					TRY_LOCK(cs_main, cs_trymain);
					// write the offer / offer accept mapping to the database
					if (!pofferdb->WriteOfferAccept(vvchArgs[1], vvchArgs[0]))
						return error( "CheckOfferInputs() : failed to write to offer DB");
					}
					
				}
				else if (op == OP_OFFER_ACCEPT) {
					
					COffer myOffer,linkOffer;
					CTransaction offerTx, linkedTx;			
					// find the payment from the tx outputs (make sure right amount of coins were paid for this offer accept), the payment amount found has to be exact	
					uint64_t heightToCheckAgainst = theOfferAccept.nHeight;
					COfferLinkWhitelistEntry entry;
					if(IsCertOp(prevOp) && found)
					{	
						theOffer.linkWhitelist.GetLinkEntryByHash(theOfferAccept.vchCertLink, entry);						
					}
					// if this accept was done via an escrow release, we get the height from escrow and use that to lookup the price at the time
					if(!theOfferAccept.vchEscrowLink.empty())
					{	
						vector<CEscrow> escrowVtxPos;
						if (pescrowdb->ExistsEscrow(theOfferAccept.vchEscrowLink)) {
							if (pescrowdb->ReadEscrow(theOfferAccept.vchEscrowLink, escrowVtxPos) && !escrowVtxPos.empty())
							{	
								// we want the initial funding escrow transaction height as when to calculate this offer accept price
								CEscrow fundingEscrow = escrowVtxPos.front();
								heightToCheckAgainst = fundingEscrow.nHeight;
							}
						}
					}

					int precision = 2;
					// lookup the price of the offer in syscoin based on pegged alias at the block # when accept/escrow was made
					int64_t nPrice = convertCurrencyCodeToSyscoin(theOffer.sCurrencyCode, theOffer.GetPrice(entry), heightToCheckAgainst, precision)*theOfferAccept.nQty;
					if(tx.vout[nOut].nValue != nPrice)
					{
						if(fDebug)
							LogPrintf("CheckOfferInputs() OP_OFFER_ACCEPT: this offer accept does not pay enough according to the offer price %ld\n", nPrice);
						return true;
					}
						
					
					if (!GetTxOfOffer(*pofferdb, vvchArgs[0], myOffer, offerTx))
						return error("CheckOfferInputs() OP_OFFER_ACCEPT: could not find an offer with this name");

					if(!myOffer.vchLinkOffer.empty())
					{
						if(!GetTxOfOffer(*pofferdb, myOffer.vchLinkOffer, linkOffer, linkedTx))
							linkOffer.SetNull();
					}
											
					// check for existence of offeraccept in txn offer obj
					if(!serializedOffer.GetAcceptByHash(vvchArgs[1], theOfferAccept))
						return error("CheckOfferInputs() OP_OFFER_ACCEPT: could not read accept from offer txn");					


					// 2 step refund: send an offer accept with nRefunded property set to inprogress and then send another with complete later
					// first step is to send inprogress so that next block we can send a complete (also sends coins during second step to original acceptor)
					// this is to ensure that the coins sent during accept are available to spend to refund to avoid having to hold double the balance of an accept amount
					// in order to refund.
					if(theOfferAccept.nQty <= 0 || (theOfferAccept.nQty > theOffer.nQty || (!linkOffer.IsNull() && theOfferAccept.nQty > linkOffer.nQty))) {
						string strError = makeOfferRefundTX(offerTx, vvchArgs[1], OFFER_REFUND_PAYMENT_INPROGRESS);
						if (strError != "" && fDebug)
							LogPrintf("CheckOfferInputs() - OP_OFFER_ACCEPT %s\n", strError.c_str());
							
						if(fDebug)
							LogPrintf("txn %s accepted but offer not fulfilled because desired qty %u is more than available qty %u\n", tx.GetHash().GetHex().c_str(), theOfferAccept.nQty, theOffer.nQty);
						return true;
					}
					if(theOffer.vchLinkOffer.empty())
					{
						theOffer.nQty -= theOfferAccept.nQty;
						// purchased a cert so xfer it
						if(!fInit && pwalletMain && IsOfferMine(offerTx) && !theOffer.vchCert.empty())
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
					if (!fInit && pwalletMain && !linkOffer.IsNull() && IsOfferMine(offerTx))
					{	
						// vchPubKey is for when transfering cert after an offer accept, the pubkey is the transfer-to address and encryption key for cert data
						// myOffer.vchLinkOffer is the linked offer guid
						// vvchArgs[1] is this offer accept rand used to walk back up and refund offers in the linked chain
						// we are now accepting the linked	 offer, up the link offer stack.

						string strError = makeOfferLinkAcceptTX(theOfferAccept, vchFromString(""), myOffer.vchLinkOffer, vvchArgs[1]);
						if(strError != "")
						{
							if(fDebug)
							{
								LogPrintf("CheckOfferInputs() - OP_OFFER_ACCEPT - makeOfferLinkAcceptTX %s\n", strError.c_str());
							}
							// if there is a problem refund this accept
							strError = makeOfferRefundTX(offerTx, vvchArgs[1], OFFER_REFUND_PAYMENT_INPROGRESS);
							if (strError != "" && fDebug)
								LogPrintf("CheckOfferInputs() - OP_OFFER_ACCEPT - makeOfferLinkAcceptTX(makeOfferRefundTX) %s\n", strError.c_str());

						}
					}
					
					
					theOfferAccept.bPaid = true;
					
					theOfferAccept.vchAcceptRand = vvchArgs[1];
					theOfferAccept.txHash = tx.GetHash();
					theOffer.PutOfferAccept(theOfferAccept);
					{
					TRY_LOCK(cs_main, cs_trymain);
					// write the offer / offer accept mapping to the database
					if (!pofferdb->WriteOfferAccept(vvchArgs[1], vvchArgs[0]))
						return error( "CheckOfferInputs() : failed to write to offer DB");
					}
				}
				
				// only modify the offer's height on an activate or update or refund
				if(op == OP_OFFER_ACTIVATE || op == OP_OFFER_UPDATE ||  op == OP_OFFER_REFUND) {
					theOffer.nHeight = chainActive.Tip()->nHeight;					
					theOffer.txHash = tx.GetHash();
					if(op == OP_OFFER_UPDATE)
					{
						// if the txn whitelist entry exists (meaning we want to remove or add)
						if(serializedOffer.linkWhitelist.entries.size() == 1)
						{
							COfferLinkWhitelistEntry entry;
							// special case we use to remove all entries
							if(serializedOffer.linkWhitelist.entries[0].nDiscountPct == -1)
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
										myLinkOffer.nHeight = theOffer.nHeight;
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


void rescanforoffers(CBlockIndex *pindexRescan) {
    LogPrintf("Scanning blockchain for offers to create fast index...\n");
    pofferdb->ReconstructOfferIndex(pindexRescan);
}



UniValue offernew(const UniValue& params, bool fHelp) {
	if (fHelp || params.size() < 7 || params.size() > 10)
		throw runtime_error(
		"offernew <alias> <category> <title> <quantity> <price> <description> <currency> [private=0] [cert. guid] [exclusive resell=1]\n"
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
						+ HelpRequiringPassphrase());
	if(!HasReachedMainNetForkB2())
		throw runtime_error("Please wait until B2 hardfork starts in before executing this command.");
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
	const CWalletTx *wtxCertIn;
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
			theCert.ClearCert();
			wtxCertIn = pwalletMain->GetWalletTx(txCert.GetHash());
			// make sure its in your wallet (you control this cert)		
			if (IsCertMine(txCert) && wtxCertIn != NULL) 
				nQty = 1;
			else
				throw runtime_error("Cannot sell this certificate, it is not yours!");
			// check the offer links in the cert, can't sell a cert thats already linked to another offer
			if(!theCert.vchOfferLink.empty())
			{
				COffer myOffer;
				CTransaction txMyOffer;
				// if offer is still valid then we cannot xfer this cert
				if (GetTxOfOffer(*pofferdb, theCert.vchOfferLink, myOffer, txMyOffer))
				{
					string strError = strprintf("Cannot sell this certificate, it is already linked to offer: %s", stringFromVch(theCert.vchOfferLink).c_str());
					throw runtime_error(strError);
				}
		    }
		}
		else
			vchCert.clear();

	}
	if(params.size() >= 10)
	{
		bExclusiveResell = atoi(params[9].get_str().c_str()) == 1? true: false;
	}
	
	int64_t nRate;
	vector<string> rateList;
	int precision;
	if(getCurrencyToSYSFromAlias(vchCurrency, nRate, chainActive.Tip()->nHeight, rateList,precision) != "")
	{
		string err = strprintf("Could not find currency %s in the SYS_RATES alias!\n", stringFromVch(vchCurrency));
		throw runtime_error(err.c_str());
	}
	float minPrice = 1/pow(10,precision);
	float price = nPrice;
	if(price < minPrice)
		price = minPrice;
	string priceStr = strprintf("%.*f", precision, price);
	nPrice = (float)atof(priceStr.c_str());
	// this is a syscoin transaction
	CWalletTx wtx;



	// generate rand identifier
	int64_t rand = GetRand(std::numeric_limits<int64_t>::max());
	vector<unsigned char> vchRand = CScriptNum(rand).getvch();
	vector<unsigned char> vchOffer = vchFromString(HexStr(vchRand));

	EnsureWalletIsUnlocked();


  	CPubKey newDefaultKey;
	CScript scriptPubKeyOrig;
	// unserialize offer UniValue from txn, serialize back
	// build offer UniValue
	COffer newOffer;
	newOffer.vchPubKey = alias.vchPubKey;
	newOffer.aliasName = aliasAddress.aliasName;
	newOffer.sCategory = vchCat;
	newOffer.sTitle = vchTitle;
	newOffer.sDescription = vchDesc;
	newOffer.nQty = nQty;
	newOffer.nHeight = chainActive.Tip()->nHeight;
	newOffer.SetPrice(nPrice);
	newOffer.vchCert = vchCert;
	newOffer.linkWhitelist.bExclusiveResell = bExclusiveResell;
	newOffer.sCurrencyCode = vchCurrency;
	newOffer.bPrivate = bPrivate;
	
	scriptPubKeyOrig= GetScriptForDestination(aliasAddress.Get());
	CScript scriptPubKey;
	scriptPubKey << CScript::EncodeOP_N(OP_OFFER_ACTIVATE) << vchOffer << OP_2DROP;
	scriptPubKey += scriptPubKeyOrig;

	vector<CRecipient> vecSend;
	CRecipient recipient;
	CreateRecipient(scriptPubKey, recipient);
	vecSend.push_back(recipient);

	const vector<unsigned char> &data = newOffer.Serialize();
	CScript scriptData;
	scriptData << OP_RETURN << data;
	CRecipient fee;
	CreateFeeRecipient(scriptData, data, fee);
	vecSend.push_back(fee);
	SendMoneySyscoin(vecSend, recipient.nAmount+fee.nAmount, false, wtx);

	UniValue res(UniValue::VARR);
	res.push_back(wtx.GetHash().GetHex());
	res.push_back(HexStr(vchRand));
	if(!theCert.IsNull() && wtxCertIn != NULL)
	{
		// get a key from our wallet set dest as ourselves
		CPubKey newDefaultKey;
		pwalletMain->GetKeyFromPool(newDefaultKey);
		scriptPubKeyOrig= GetScriptForDestination(newDefaultKey.GetID());

		// create CERTUPDATE txn keys
		CScript scriptPubKey;
		scriptPubKey << CScript::EncodeOP_N(OP_CERT_UPDATE) << vchCert << OP_2DROP;
		scriptPubKey += scriptPubKeyOrig;

		theCert.nHeight = chainActive.Tip()->nHeight;
		theCert.vchOfferLink = vchOffer;
		vector<CRecipient> vecSend;
		CRecipient recipient;
		CreateRecipient(scriptPubKey, recipient);
		vecSend.push_back(recipient);

		const vector<unsigned char> &data = theCert.Serialize();
		CScript scriptData;
		scriptData << OP_RETURN << data;
		CRecipient fee;
		CreateFeeRecipient(scriptData, data, fee);
		vecSend.push_back(fee);
		SendMoneySyscoin(vecSend, recipient.nAmount+fee.nAmount, false, wtx, wtxCertIn);

	}
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
	if(!HasReachedMainNetForkB2())
		throw runtime_error("Please wait until B2 hardfork starts in before executing this command.");
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
			if (IsCertMine(txCert) && wtxCertIn != NULL) 
			{
				foundEntry = entry;
				break;	
			}
			
		}

	}


	// if the whitelist exclusive mode is on and you dont have a cert in the whitelist, you cannot link to this offer
	if(foundEntry.IsNull() && linkOffer.linkWhitelist.bExclusiveResell)
	{
		throw runtime_error("cannot link to this offer because you don't own a cert from its whitelist (the offer is in exclusive mode)");
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
	float minPrice = 1/pow(10,precision);
	float price = linkOffer.GetPrice();
	if(price < minPrice)
		price = minPrice;

	EnsureWalletIsUnlocked();
	
	// unserialize offer UniValue from txn, serialize back
	// build offer UniValue
	COffer newOffer;
	newOffer.vchPubKey = linkOffer.vchPubKey;
	newOffer.aliasName = linkOffer.aliasName;
	newOffer.sCategory = linkOffer.sCategory;
	newOffer.sTitle = linkOffer.sTitle;
	newOffer.sDescription = vchDesc;
	newOffer.nQty = linkOffer.nQty;
	newOffer.linkWhitelist.bExclusiveResell = true;
	newOffer.SetPrice(price);
	
	newOffer.nCommission = commissionInteger;
	
	newOffer.vchLinkOffer = vchLinkOffer;
	newOffer.nHeight = chainActive.Tip()->nHeight;
	newOffer.sCurrencyCode = linkOffer.sCurrencyCode;
	
	//create offeractivate txn keys
	CPubKey newDefaultKey;
	pwalletMain->GetKeyFromPool(newDefaultKey);
	CScript scriptPubKeyOrig;
	scriptPubKeyOrig= GetScriptForDestination(newDefaultKey.GetID());
	CScript scriptPubKey;
	scriptPubKey << CScript::EncodeOP_N(OP_OFFER_ACTIVATE) << vchOffer << OP_2DROP;
	scriptPubKey += scriptPubKeyOrig;


	string strError;

	vector<CRecipient> vecSend;
	CRecipient recipient;
	CreateRecipient(scriptPubKey, recipient);
	vecSend.push_back(recipient);

	const vector<unsigned char> &data = newOffer.Serialize();
	CScript scriptData;
	scriptData << OP_RETURN << data;
	CRecipient fee;
	CreateFeeRecipient(scriptData, data, fee);
	vecSend.push_back(fee);
	SendMoneySyscoin(vecSend, recipient.nAmount+fee.nAmount, false, wtx);

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

	if(!HasReachedMainNetForkB2())
		throw runtime_error("Please wait until B2 hardfork starts in before executing this command.");	
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
	const CWalletTx* wtxCertIn;
	if (!GetTxOfCert(*pcertdb, vchCert, theCert, txCert))
		throw runtime_error("could not find a certificate with this key");

	// check to see if certificate in wallet
	wtxCertIn = pwalletMain->GetWalletTx(txCert.GetHash());
	if (wtxCertIn == NULL)
		throw runtime_error("this certificate is not in your wallet");


	// this is a syscoind txn
	CScript scriptPubKeyOrig;

	// get a key from our wallet set dest as ourselves
	CPubKey newDefaultKey;
	pwalletMain->GetKeyFromPool(newDefaultKey);
	scriptPubKeyOrig= GetScriptForDestination(newDefaultKey.GetID());

	EnsureWalletIsUnlocked();

	// look for a transaction with this key
	CTransaction tx;
	COffer theOffer;
	if (!GetTxOfOffer(*pofferdb, vchOffer, theOffer, tx))
		throw runtime_error("could not find an offer with this name");
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
	theOffer.nHeight = chainActive.Tip()->nHeight;
	for(unsigned int i=0;i<theOffer.linkWhitelist.entries.size();i++) {
		COfferLinkWhitelistEntry& entry = theOffer.linkWhitelist.entries[i];
		// make sure this cert doesn't already exist
		if (entry.certLinkVchRand == vchCert)
		{
			throw runtime_error("This cert is already added to your whitelist");
		}

	}
	// create OFFERUPDATE txn keys
	CScript scriptPubKey;
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
	const vector<unsigned char> &data = theOffer.Serialize();
	CScript scriptData;
	scriptData << OP_RETURN << data;
	CRecipient fee;
	CreateFeeRecipient(scriptData, data, fee);
	vecSend.push_back(fee);
	SendMoneySyscoin(vecSend, recipient.nAmount+fee.nAmount, false, wtx, wtxIn);

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
	if(!HasReachedMainNetForkB2())
		throw runtime_error("Please wait until B2 hardfork starts in before executing this command.");	
	// gather & validate inputs
	vector<unsigned char> vchOffer = vchFromValue(params[0]);
	vector<unsigned char> vchCert = vchFromValue(params[1]);


	CCert theCert;

	// this is a syscoind txn
	CWalletTx wtx;
	const CWalletTx* wtxIn;
	CScript scriptPubKeyOrig;

	// get a key from our wallet set dest as ourselves
	CPubKey newDefaultKey;
	pwalletMain->GetKeyFromPool(newDefaultKey);
	scriptPubKeyOrig= GetScriptForDestination(newDefaultKey.GetID());

	EnsureWalletIsUnlocked();

	// look for a transaction with this key
	CTransaction tx;
	COffer theOffer;
	if (!GetTxOfOffer(*pofferdb, vchOffer, theOffer, tx))
		throw runtime_error("could not find an offer with this name");

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
	SendMoneySyscoin(vecSend, recipient.nAmount+fee.nAmount, false, wtx, wtxIn);

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
	if(!HasReachedMainNetForkB2())
		throw runtime_error("Please wait until B2 hardfork starts in before executing this command.");	
	// gather & validate inputs
	vector<unsigned char> vchOffer = vchFromValue(params[0]);


	CCert theCert;

	// this is a syscoind txn
	CWalletTx wtx;
	const CWalletTx* wtxIn;
	CScript scriptPubKeyOrig;

	// get a key from our wallet set dest as ourselves
	CPubKey newDefaultKey;
	pwalletMain->GetKeyFromPool(newDefaultKey);
	scriptPubKeyOrig= GetScriptForDestination(newDefaultKey.GetID());

	EnsureWalletIsUnlocked();

	// look for a transaction with this key
	CTransaction tx;
	COffer theOffer;
	if (!GetTxOfOffer(*pofferdb, vchOffer, theOffer, tx))
		throw runtime_error("could not find an offer with this name");

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
	entry.nDiscountPct = -1;
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
	SendMoneySyscoin(vecSend, recipient.nAmount+fee.nAmount, false, wtx, wtxIn);

	UniValue res(UniValue::VARR);
	res.push_back(wtx.GetHash().GetHex());
	return res;
}

UniValue offerwhitelist(const UniValue& params, bool fHelp) {
    if (fHelp || 1 != params.size())
        throw runtime_error("offerwhitelist <offer guid>\n"
                "List all whitelist entries for this offer.\n");
	if(!HasReachedMainNetForkB2())
		throw runtime_error("Please wait until B2 hardfork starts in before executing this command.");	
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
		uint256 blockHash;
		if (GetTxOfCert(*pcertdb, entry.certLinkVchRand, theCert, txCert))
		{
			UniValue oList(UniValue::VOBJ);
			oList.push_back(Pair("cert_guid", stringFromVch(entry.certLinkVchRand)));
			oList.push_back(Pair("cert_title", stringFromVch(theCert.vchTitle)));
			oList.push_back(Pair("cert_is_mine", IsCertMine(txCert) ? "true" : "false"));
			string strAddress = "";
			GetCertAddress(txCert, strAddress);
			oList.push_back(Pair("cert_address", strAddress));
			int expires_in = 0;
			if (!GetTransaction(txCert.GetHash(), txCert, Params().GetConsensus(), blockHash, true))
				continue;
			uint64_t nHeight = theCert.nHeight;
            if(nHeight + GetCertExpirationDepth() - chainActive.Tip()->nHeight > 0)
			{
				expires_in = nHeight + GetCertExpirationDepth() - chainActive.Tip()->nHeight;
			}  
			oList.push_back(Pair("cert_expiresin",expires_in));
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

	if(!HasReachedMainNetForkB2())
		throw runtime_error("Please wait until B2 hardfork starts in before executing this command.");	
	// gather & validate inputs
	vector<unsigned char> vchOffer = vchFromValue(params[0]);
	vector<unsigned char> vchCat = vchFromValue(params[1]);
	vector<unsigned char> vchTitle = vchFromValue(params[2]);
	vector<unsigned char> vchDesc;
	vector<unsigned char> vchCert;
	vector<unsigned char> vchOldCert;
	bool bExclusiveResell = true;
	int bPrivate = false;
	unsigned int nQty;
	float price;
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
	CScript scriptPubKeyOrig;

	// get a key from our wallet set dest as ourselves
	CPubKey newDefaultKey;
	pwalletMain->GetKeyFromPool(newDefaultKey);
	scriptPubKeyOrig= GetScriptForDestination(newDefaultKey.GetID());

	// create OFFERUPDATE txn keys
	CScript scriptPubKey;
	scriptPubKey << CScript::EncodeOP_N(OP_OFFER_UPDATE) << vchOffer << OP_2DROP;
	scriptPubKey += scriptPubKeyOrig;

	EnsureWalletIsUnlocked();

	// look for a transaction with this key
	CTransaction tx, linktx;
	COffer theOffer, linkOffer;
	if (!GetTxOfOffer(*pofferdb, vchOffer, theOffer, tx))
		throw runtime_error("could not find an offer with this name");

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
	COffer offerCopy = theOffer;
	theOffer.ClearOffer();	
	int precision = 2;
	// get precision
	convertCurrencyCodeToSyscoin(theOffer.sCurrencyCode, theOffer.GetPrice(), chainActive.Tip()->nHeight, precision);
	float minPrice = 1/pow(10,precision);
	if(price < minPrice)
		price = minPrice;
	string priceStr = strprintf("%.*f", precision, price);
	price = (float)atof(priceStr.c_str());
	// update offer values
	if(offerCopy.sCategory != vchCat)
		theOffer.sCategory = vchCat;
	if(offerCopy.sTitle != vchTitle)
		theOffer.sTitle = vchTitle;
	if(offerCopy.sDescription != vchDesc)
		theOffer.sDescription = vchDesc;
	if (params.size() >= 7)
		theOffer.bPrivate = bPrivate;
	unsigned int memPoolQty = QtyOfPendingAcceptsInMempool(vchOffer);
	if((nQty-memPoolQty) < 0)
		throw runtime_error("not enough remaining quantity to fulfill this offerupdate"); // SS i think needs better msg
	vchOldCert = theOffer.vchCert;
	if(vchCert.empty())
	{
		theOffer.nQty = nQty;
	}	
	else
	{
		theOffer.nQty = 1;
		if(theOffer.vchCert != vchCert)
			theOffer.vchCert = vchCert;
	}
	// update cert if offer has a cert
	if(!vchCert.empty())
	{
		CTransaction txCert;
		CWalletTx wtx;
		const CWalletTx *wtxCertIn;
		CCert theCert;
		// make sure this cert is still valid
		if (GetTxOfCert(*pcertdb, vchCert, theCert, txCert))
		{
			theCert.ClearCert();
			// make sure its in your wallet (you control this cert)	
			wtxCertIn = pwalletMain->GetWalletTx(txCert.GetHash());
			if (!IsCertMine(txCert) || wtxCertIn == NULL) 
			{
				throw runtime_error("Cannot update this offer because this certificate is not yours!");
			}			
		}
		else
			throw runtime_error("Cannot find this certificate!");
		
		if(!theCert.vchOfferLink.empty() && theCert.vchOfferLink != vchOffer)
		{
			COffer myOffer;
			CTransaction txMyOffer;
			// if offer is still valid then we cannot xfer this cert
			if (GetTxOfOffer(*pofferdb, theCert.vchOfferLink, myOffer, txMyOffer))
			{
				string strError = strprintf("Cannot update this offer because this certificate is linked to another offer: %s", stringFromVch(theCert.vchOfferLink).c_str());
				throw runtime_error(strError);
			}
		}

		// get a key from our wallet set dest as ourselves
		CPubKey newDefaultKey;
		pwalletMain->GetKeyFromPool(newDefaultKey);
		scriptPubKeyOrig= GetScriptForDestination(newDefaultKey.GetID());

		// create CERTUPDATE txn keys
		CScript scriptPubKey;
		scriptPubKey << CScript::EncodeOP_N(OP_CERT_UPDATE) << vchCert << OP_2DROP;
		scriptPubKey += scriptPubKeyOrig;


		theCert.nHeight = chainActive.Tip()->nHeight;
		if(vchCert.empty())
		{
			theOffer.vchCert.clear();
			theCert.vchOfferLink.clear();
		}
		else
			theCert.vchOfferLink = vchOffer;

		vector<CRecipient> vecSend;
		CRecipient recipient;
		CreateRecipient(scriptPubKey, recipient);
		vecSend.push_back(recipient);

		const vector<unsigned char> &data = theCert.Serialize();
		CScript scriptData;
		scriptData << OP_RETURN << data;
		CRecipient fee;
		CreateFeeRecipient(scriptData, data, fee);
		vecSend.push_back(fee);
		SendMoneySyscoin(vecSend, recipient.nAmount+fee.nAmount, false, wtx, wtxCertIn);

		// updating from one cert to another, need to update both certs in this case
		if(!vchOldCert.empty() && !theOffer.vchCert.empty() && theOffer.vchCert != vchOldCert)
		{
			// this offer changed certs so remove offer link from old cert
			CTransaction txOldCert;
			CWalletTx wtxold;
			const CWalletTx* wtxCertOldIn;
			CCert theOldCert;
			// make sure this cert is still valid
			if (GetTxOfCert(*pcertdb, vchOldCert, theOldCert, txOldCert))
			{
				theOldCert.ClearCert();
				// make sure its in your wallet (you control this cert)		
				wtxCertOldIn = pwalletMain->GetWalletTx(txOldCert.GetHash());
				if (!IsCertMine(txOldCert) || wtxCertOldIn == NULL)
				{
					throw runtime_error("Cannot update this offer because old certificate is not yours!");
				}			
			}
			else
				throw runtime_error("Cannot find old certificate!");
			// create CERTUPDTE txn keys
			CScript scriptOldPubKey, scriptPubKeyOld;
			pwalletMain->GetKeyFromPool(newDefaultKey);
			scriptPubKeyOld= GetScriptForDestination(newDefaultKey.GetID());
			
			scriptOldPubKey << CScript::EncodeOP_N(OP_CERT_UPDATE) << vchOldCert << OP_2DROP;
			scriptOldPubKey += scriptPubKeyOld;
			// clear the offer link, this is the only change we are making to this cert
			theOldCert.vchOfferLink.clear();
			theOldCert.nHeight = chainActive.Tip()->nHeight;


			vector<CRecipient> vecSend;
			CRecipient recipient;
			CreateRecipient(scriptOldPubKey, recipient);
			vecSend.push_back(recipient);

			const vector<unsigned char> &data = theOldCert.Serialize();
			CScript scriptData;
			scriptData << OP_RETURN << data;
			CRecipient fee;
			CreateFeeRecipient(scriptData, data, fee);
			vecSend.push_back(fee);
			SendMoneySyscoin(vecSend, recipient.nAmount+fee.nAmount, false, wtxold, wtxCertOldIn);
		}
	}
	theOffer.nHeight = chainActive.Tip()->nHeight;
	theOffer.SetPrice(price);
	
	if(params.size() >= 9)
		theOffer.linkWhitelist.bExclusiveResell = bExclusiveResell;


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
	SendMoneySyscoin(vecSend, recipient.nAmount+fee.nAmount, false, wtx, wtxIn);

	UniValue res(UniValue::VARR);
	res.push_back(wtx.GetHash().GetHex());
	return res;
}

UniValue offerrefund(const UniValue& params, bool fHelp) {
	if (fHelp || 1 != params.size())
		throw runtime_error("offerrefund <acceptguid>\n"
				"Refund an offer accept of an offer you control.\n"
				"<guid> guidkey of offer accept to refund.\n"
				+ HelpRequiringPassphrase());
	if(!HasReachedMainNetForkB2())
		throw runtime_error("Please wait until B2 hardfork starts in before executing this command.");
	vector<unsigned char> vchAcceptRand = vchFromString(params[0].get_str());

	EnsureWalletIsUnlocked();

	// look for a transaction with this key
	COffer theOffer;
	CTransaction txOffer;
	COfferAccept theOfferAccept;
	uint256 hashBlock;

	if (!GetTxOfOfferAccept(*pofferdb, vchAcceptRand, theOffer, txOffer))
		throw runtime_error("could not find an offer accept with this identifier");
	vector<vector<unsigned char> > vvch;
	int op, nOut;
	
	if(DecodeOfferTx(txOffer, op, nOut, vvch, -1)) 
		throw runtime_error("could not could offer tx");
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
	
	string strError = makeOfferRefundTX(txOffer, vchAcceptRand, OFFER_REFUND_PAYMENT_INPROGRESS);
	if (strError != "")
	{
		throw runtime_error(strError);
	}
	return "Success";
}

UniValue offeraccept(const UniValue& params, bool fHelp) {
	if (fHelp || 1 > params.size() || params.size() > 8)
		throw runtime_error("offeraccept <guid> [quantity] [pubkey] [message] [refund address] [linkedguid] [escrowTxHash] [height]\n"
				"Accept&Pay for a confirmed offer.\n"
				"<guid> guidkey from offer.\n"
				"<pubkey> Public key of buyer address (to transfer certificate).\n"
				"<quantity> quantity to buy. Defaults to 1.\n"
				"<message> payment message to seller, 1KB max.\n"
				"<refund address> In case offer not accepted refund to this address. Leave empty to use a new address from your wallet. \n"
				"<linkedguid> guidkey from offer accept linking to this offer accept. For internal use only, leave blank\n"
				"<escrowTxHash> If this offer accept is done by an escrow release. For internal use only, leave blank\n"
				"<height> Height to index into price calculation function. For internal use only, leave blank\n"
				+ HelpRequiringPassphrase());
	if(!HasReachedMainNetForkB2())
		throw runtime_error("Please wait until B2 hardfork starts in before executing this command.");	
	vector<unsigned char> vchRefundAddress;	
	CSyscoinAddress refundAddr;	
	vector<unsigned char> vchOffer = vchFromValue(params[0]);
	vector<unsigned char> vchPubKey = vchFromValue(params.size()>=3?params[2]:"");
	vector<unsigned char> vchLinkOfferAccept = vchFromValue(params.size()>= 6? params[5]:"");
	vector<unsigned char> vchMessage = vchFromValue(params.size()>=4?params[3]:"");
	vector<unsigned char> vchEscrowTxHash = vchFromValue(params.size()>=7?params[6]:"");
	int64_t nHeight = params.size()>=8?atoi64(params[7].get_str()):0;
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
	if(params.size() < 5)
	{
		CPubKey newDefaultKey;
		pwalletMain->GetKeyFromPool(newDefaultKey);
		refundAddr = CSyscoinAddress(newDefaultKey.GetID());
		vchRefundAddress = vchFromString(refundAddr.ToString());
	}
	else
	{
		vchRefundAddress = vchFromValue(params[4]);
		refundAddr = CSyscoinAddress(stringFromVch(vchRefundAddress));
	}
    if (vchMessage.size() <= 0 && vchPubKey.empty())
        throw runtime_error("offeraccept message data cannot be empty!");

	// this is a syscoin txn
	CWalletTx wtx;
	CScript scriptPubKeyOrig;

	// generate offer accept identifier and hash
	int64_t rand = GetRand(std::numeric_limits<int64_t>::max());
	vector<unsigned char> vchAcceptRand = CScriptNum(rand).getvch();
	vector<unsigned char> vchAccept = vchFromString(HexStr(vchAcceptRand));

	// create OFFERACCEPT txn keys
	CScript scriptPubKey;
	scriptPubKey << CScript::EncodeOP_N(OP_OFFER_ACCEPT) << vchOffer << vchAccept << OP_2DROP << OP_DROP;

	EnsureWalletIsUnlocked();
	const CWalletTx *wtxEscrowIn;
	CEscrow escrow;
	vector<vector<unsigned char> > escrowVvch;
	if(!vchEscrowTxHash.empty())
	{
		uint256 escrowTxHash(uint256S(stringFromVch(vchEscrowTxHash)));
		// make sure escrow is in wallet
		wtxEscrowIn = pwalletMain->GetWalletTx(escrowTxHash);
		if (wtxEscrowIn == NULL) 
			throw runtime_error("release escrow transaction is not in your wallet");;
		if(!escrow.UnserializeFromTx(*wtxEscrowIn))
			throw runtime_error("release escrow transaction cannot unserialize escrow value");
		
		int op, nOut;
		if (!DecodeEscrowTx(*wtxEscrowIn, op, nOut, escrowVvch, -1))
			throw runtime_error("Cannot decode escrow tx hash");
	}
	if (ExistsInMempool(vchOffer, OP_OFFER_REFUND) || ExistsInMempool(vchOffer, OP_OFFER_ACTIVATE) || ExistsInMempool(vchOffer, OP_OFFER_UPDATE)) {
		throw runtime_error("there are pending operations or refunds on that offer");
	}
	// if we want to accept an escrow release or we are accepting a linked offer from an escrow release. Override heightToCheckAgainst if using escrow since escrow can take a long time.
	if(!escrow.IsNull() && escrowVvch.size() > 0)
	{
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
	// look for a transaction with this key
	CTransaction tx;
	COffer theOffer;
	if (!GetTxOfOffer(*pofferdb, vchOffer, theOffer, tx))
	{
		throw runtime_error("could not find an offer with this identifier");
	}
	COffer linkedOffer;
	CTransaction tmpTx;
	// check if parent to linked offer is still valid
	if (!theOffer.vchLinkOffer.empty())
	{
		if(pofferdb->ExistsOffer(theOffer.vchLinkOffer))
		{
			if (!GetTxOfOffer(*pofferdb, theOffer.vchLinkOffer, linkedOffer, tmpTx))
				throw runtime_error("Trying to accept a linked offer but could not find parent offer, perhaps it is expired");
		}
	}
	COfferLinkWhitelistEntry foundCert;
	const CWalletTx *wtxCertIn = NULL;
	// go through the whitelist and see if you own any of the certs to apply to this offer for a discount
	for(unsigned int i=0;i<theOffer.linkWhitelist.entries.size();i++) {
		CTransaction txCert;
		
		CCert theCert;
		COfferLinkWhitelistEntry& entry = theOffer.linkWhitelist.entries[i];
		// make sure this cert is still valid
		if (GetTxOfCert(*pcertdb, entry.certLinkVchRand, theCert, txCert))
		{
			// make sure its in your wallet (you control this cert)
			wtxCertIn = pwalletMain->GetWalletTx(txCert.GetHash());		
			if (IsCertMine(txCert) && wtxCertIn != NULL) 
			{
				foundCert = entry;
				break;
			}		
		}
	}
	// if this is an accept for a linked offer, the offer is set to exclusive mode and you dont have a cert in the whitelist, you cannot accept this offer
	if(!vchLinkOfferAccept.empty() && foundCert.IsNull() && theOffer.linkWhitelist.bExclusiveResell)
	{
		throw runtime_error("cannot pay for this linked offer because you don't own a cert from its whitelist");
	}

	unsigned int memPoolQty = QtyOfPendingAcceptsInMempool(vchOffer);
	if(theOffer.nQty < (nQty+memPoolQty))
		throw runtime_error("not enough remaining quantity to fulfill this orderaccept");

	int precision = 2;
	int64_t nPrice = convertCurrencyCodeToSyscoin(theOffer.sCurrencyCode, theOffer.GetPrice(foundCert), nHeight>0?nHeight:chainActive.Tip()->nHeight, precision);
	string strCipherText = "";
	// encryption should only happen once even when not a resell or not an escrow accept. It is already encrypted in both cases.
	if(vchLinkOfferAccept.empty() && vchEscrowTxHash.empty())
	{
		// encrypt to offer owner
		if(!EncryptMessage(theOffer.vchPubKey, vchMessage, strCipherText))
			throw runtime_error("could not encrypt message to seller");
	}
	if (strCipherText.size() > MAX_ENCRYPTED_VALUE_LENGTH)
		throw runtime_error("offeraccept message length cannot exceed 1023 bytes!");
	if(!theOffer.vchCert.empty())
	{
		CTransaction txCert;
		CCert theCert;
		// make sure this cert is still valid
		if (!GetTxOfCert(*pcertdb, theOffer.vchCert, theCert, txCert))
		{
			// make sure its in your wallet (you control this cert)		
			throw runtime_error("Cannot purchase this certificate, it may be expired!");
			
		}
		nQty = 1;
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
	txAccept.vchRefundAddress = vchRefundAddress;
	// if we have a linked offer accept then use height from linked accept (the one buyer makes, not the reseller). We need to do this to make sure we convert price at the time of initial buyer's accept.
	// in checkescrowinput we override this if its from an escrow release, just like above.
	txAccept.nHeight = nHeight>0?nHeight:chainActive.Tip()->nHeight;

	txAccept.bPaid = true;
	txAccept.vchCertLink = foundCert.certLinkVchRand;
	txAccept.vchBuyerKey = vchPubKey;
	if(!escrowVvch.empty())
		txAccept.vchEscrowLink = escrowVvch[0];
	theOffer.ClearOffer();
	theOffer.PutOfferAccept(txAccept);

    int64_t nTotalValue = ( nPrice * nQty );
    

    CScript scriptPayment;
	string strAddress = "";
    GetOfferAddress(tx, strAddress);
	CSyscoinAddress address(strAddress);
	if(!address.IsValid())
	{
		throw runtime_error("payment to invalid address");
	}
    scriptPayment= GetScriptForDestination(address.Get());
	scriptPubKey += scriptPayment;

	vector<CRecipient> vecSend;
	CRecipient paymentRecipient = {scriptPubKey, nTotalValue, false};
	vecSend.push_back(paymentRecipient);

	const vector<unsigned char> &data = theOffer.Serialize();
	CScript scriptData;
	scriptData << OP_RETURN << data;
	CRecipient fee;
	CreateFeeRecipient(scriptData, data, fee);
	vecSend.push_back(fee);
	if(!foundCert.IsNull())
	{
		if(escrow.IsNull() && wtxCertIn != NULL)
		{
			SendMoneySyscoin(vecSend, fee.nAmount+nTotalValue, false, wtx, wtxCertIn);
		}
		// create a certupdate passing in wtx (offeraccept) as input to keep chain of inputs going for next cert transaction (since we used the last cert tx as input to sendmoneysysoin)
		CWalletTx wtxCert;
		CScript scriptPubKeyOrig;
		CCert cert;
		CTransaction tx;
		if (!GetTxOfCert(*pcertdb, foundCert.certLinkVchRand, cert, tx))
			throw runtime_error("could not find a certificate with this key");
		cert.ClearCert();
		// get a key from our wallet set dest as ourselves
		CPubKey newDefaultKey;
		pwalletMain->GetKeyFromPool(newDefaultKey);
		scriptPubKeyOrig = GetScriptForDestination(newDefaultKey.GetID());

		// create CERTUPDATE txn keys
		CScript scriptPubKey;
		scriptPubKey << CScript::EncodeOP_N(OP_CERT_UPDATE) << foundCert.certLinkVchRand << OP_2DROP;
		scriptPubKey += scriptPubKeyOrig;
		cert.nHeight = chainActive.Tip()->nHeight;

		vector<CRecipient> vecSend;
		CRecipient recipient;
		CreateRecipient(scriptPubKey, recipient);
		vecSend.push_back(recipient);
		const vector<unsigned char> &data = cert.Serialize();
		CScript scriptData;
		scriptData << OP_RETURN << data;
		CRecipient fee;
		CreateFeeRecipient(scriptData, data, fee);
		vecSend.push_back(fee);
		const CWalletTx* wtxIn = &wtx;
		SendMoneySyscoin(vecSend, recipient.nAmount+fee.nAmount, false, wtxCert, wtxIn);
	}
	else
	{
		SendMoneySyscoin(vecSend, paymentRecipient.nAmount+fee.nAmount, false, wtx);
	}
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
	string offer = stringFromVch(vchOffer);
	{
		vector<COffer> vtxPos;
		if (!pofferdb->ReadOffer(vchOffer, vtxPos))
			throw runtime_error("failed to read from offer DB");
		if (vtxPos.size() < 1)
			throw runtime_error("no result returned");

        // get transaction pointed to by offer
        CTransaction tx;
        uint256 blockHash;
        uint256 txHash = vtxPos.back().txHash;
        if (!GetTransaction(txHash, tx, Params().GetConsensus(), blockHash, true))
            throw runtime_error("failed to read transaction from disk");

        COffer theOffer = vtxPos.back();

		UniValue oOffer(UniValue::VOBJ);
		vector<unsigned char> vchValue;
		UniValue aoOfferAccepts(UniValue::VARR);
		for(unsigned int i=0;i<theOffer.accepts.size();i++) {
			COfferAccept ca = theOffer.accepts[i];
			UniValue oOfferAccept(UniValue::VOBJ);

	        // get transaction pointed to by offer

	        CTransaction txA;
	        uint256 blockHashA;
	        uint256 txHashA= ca.txHash;
	        if (!GetTransaction(txHashA, txA, Params().GetConsensus(), blockHashA, true))
	            throw runtime_error("failed to read transaction from disk");
            vector<vector<unsigned char> > vvch;
            int op, nOut;
            if (!DecodeOfferTx(txA, op, nOut, vvch, -1) 
            	|| !IsOfferOp(op) 
            	|| (op != OP_OFFER_ACCEPT))
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
			oOfferAccept.push_back(Pair("txid", ca.txHash.GetHex()));
			oOfferAccept.push_back(Pair("height", sHeight));
			oOfferAccept.push_back(Pair("time", sTime));
			oOfferAccept.push_back(Pair("quantity", strprintf("%u", ca.nQty)));
			oOfferAccept.push_back(Pair("currency", stringFromVch(theOffer.sCurrencyCode)));
			int precision = 2;
			int64_t nPricePerUnit = convertCurrencyCodeToSyscoin(theOffer.sCurrencyCode, ca.nPrice, ca.nHeight, precision);
			oOfferAccept.push_back(Pair("systotal", ValueFromAmount(nPricePerUnit * ca.nQty)));
			oOfferAccept.push_back(Pair("sysprice", ValueFromAmount(nPricePerUnit)));
			oOfferAccept.push_back(Pair("price", strprintf("%.*f", precision, ca.nPrice ))); 	
			oOfferAccept.push_back(Pair("total", strprintf("%.*f", precision, ca.nPrice * ca.nQty )));
			COfferLinkWhitelistEntry entry;
			if(IsOfferMine(tx)) 
				theOffer.linkWhitelist.GetLinkEntryByHash(ca.vchCertLink, entry);
			oOfferAccept.push_back(Pair("offer_discount_percentage", strprintf("%d%%", entry.nDiscountPct)));
			oOfferAccept.push_back(Pair("is_mine", IsOfferMine(txA) ? "true" : "false"));
			if(ca.bPaid) {
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
				oOfferAccept.push_back(Pair("refund_txid", ca.txRefundId.GetHex()));
			}
			else
			{
				oOfferAccept.push_back(Pair("refunded", "false"));
				oOfferAccept.push_back(Pair("refund_txid", ""));
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
		string strAddress = "";
        GetOfferAddress(tx, strAddress);
		CSyscoinAddress address(strAddress);
		if(address.IsValid() && address.isAlias)
			oOffer.push_back(Pair("address", address.aliasName));
		else
			oOffer.push_back(Pair("address", address.ToString()));
		oOffer.push_back(Pair("category", stringFromVch(theOffer.sCategory)));
		oOffer.push_back(Pair("title", stringFromVch(theOffer.sTitle)));
		oOffer.push_back(Pair("quantity", strprintf("%u", theOffer.nQty)));
		oOffer.push_back(Pair("currency", stringFromVch(theOffer.sCurrencyCode)));
		
		
		int precision = 2;
		int64_t nPricePerUnit = convertCurrencyCodeToSyscoin(theOffer.sCurrencyCode, theOffer.GetPrice(), nHeight, precision);
		oOffer.push_back(Pair("sysprice", ValueFromAmount(nPricePerUnit)));
		oOffer.push_back(Pair("price", strprintf("%.*f", precision, theOffer.GetPrice() ))); 
		
		oOffer.push_back(Pair("is_mine", IsOfferMine(tx) ? "true" : "false"));
		if(!theOffer.vchLinkOffer.empty() && IsOfferMine(tx)) {
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
		oOffer.push_back(Pair("description", stringFromVch(theOffer.sDescription)));
		oOffer.push_back(Pair("alias", theOffer.aliasName));
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
            if (!DecodeOfferTx(wtx, op, nOut, vvch, -1) 
            	|| !IsOfferOp(op) 
            	|| (op != OP_OFFER_ACCEPT))
                continue;
			if(vvch[0] != vchOfferToFind && !vchOfferToFind.empty())
				continue;
            vchOffer = vvch[0];
			
			COfferAccept theOfferAccept;

			// Check hash
			const vector<unsigned char> &vchAcceptRand = vvch[1];			

			CTransaction offerTx;
			COffer theOffer;

			if(!GetTxOfOffer(*pofferdb, vchOffer, theOffer, offerTx))	
				continue;

			// check for existence of offeraccept in txn offer obj
			if(!theOffer.GetAcceptByHash(vchAcceptRand, theOfferAccept))
				continue;					
			string offer = stringFromVch(vchOffer);
			string sHeight = strprintf("%llu", theOfferAccept.nHeight);
			oOfferAccept.push_back(Pair("offer", offer));
			oOfferAccept.push_back(Pair("title", stringFromVch(theOffer.sTitle)));
			oOfferAccept.push_back(Pair("id", stringFromVch(vchAcceptRand)));
			oOfferAccept.push_back(Pair("linkofferaccept", stringFromVch(theOfferAccept.vchLinkOfferAccept)));
			oOfferAccept.push_back(Pair("alias", theOffer.aliasName));
			oOfferAccept.push_back(Pair("buyerkey", stringFromVch(theOfferAccept.vchBuyerKey)));
			oOfferAccept.push_back(Pair("height", sHeight));
			oOfferAccept.push_back(Pair("quantity", strprintf("%u", theOfferAccept.nQty)));
			oOfferAccept.push_back(Pair("currency", stringFromVch(theOffer.sCurrencyCode)));
			COfferLinkWhitelistEntry entry;
			if(IsOfferMine(offerTx)) 
				theOffer.linkWhitelist.GetLinkEntryByHash(theOfferAccept.vchCertLink, entry);
			oOfferAccept.push_back(Pair("offer_discount_percentage", strprintf("%d%%", entry.nDiscountPct)));
			oOfferAccept.push_back(Pair("escrowlink", stringFromVch(theOfferAccept.vchEscrowLink)));
			int precision = 2;
			int64_t nPricePerUnit = convertCurrencyCodeToSyscoin(theOffer.sCurrencyCode, theOfferAccept.nPrice, theOfferAccept.nHeight, precision);
			oOfferAccept.push_back(Pair("systotal", ValueFromAmount(nPricePerUnit * theOfferAccept.nQty)));
			oOfferAccept.push_back(Pair("price", strprintf("%.*f", precision, theOfferAccept.nPrice ))); 
			oOfferAccept.push_back(Pair("total", strprintf("%.*f", precision, theOfferAccept.nPrice * theOfferAccept.nQty ))); 
			// this accept is for me(something ive sold) if this offer is mine
			oOfferAccept.push_back(Pair("is_mine", IsOfferMine(offerTx)? "true" : "false"));
			if(theOfferAccept.bPaid && !theOfferAccept.bRefunded) {
				oOfferAccept.push_back(Pair("status","paid"));
			}
			else if(!theOfferAccept.bRefunded)
			{
				oOfferAccept.push_back(Pair("status","not paid"));
			}
			else if(theOfferAccept.bRefunded) { 
				oOfferAccept.push_back(Pair("status", "refunded"));
			}
			string strMessage = string("");
			if(!DecryptMessage(theOffer.vchPubKey, theOfferAccept.vchMessage, strMessage))
				strMessage = string("Encrypted for owner of offer");
			oOfferAccept.push_back(Pair("pay_message", strMessage));
			oRes.push_back(oOfferAccept);	
        }
       BOOST_FOREACH(PAIRTYPE(const uint256, CWalletTx)& item, pwalletMain->mapWallet)
        {
			UniValue oOfferAccept(UniValue::VOBJ);
            // get txn hash, read txn index
            hash = item.second.GetHash();

            if (!GetTransaction(hash, tx, Params().GetConsensus(), blockHash, true))
                continue;
            if (tx.nVersion != SYSCOIN_TX_VERSION)
                continue;

            // decode txn
            vector<vector<unsigned char> > vvch;
            int op, nOut;
            if (!DecodeEscrowTx(tx, op, nOut, vvch, -1) 
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
			if (!GetTxOfOfferAccept(*pofferdb, theEscrow.vchOfferAcceptLink, theOffer, offerTx))
				continue;

            if (!GetTransaction(theOfferAccept.txHash, acceptTx, Params().GetConsensus(), blockHash, true))
                continue;
            // decode txn, skip non-alias txns
            vector<vector<unsigned char> > offerVvch;
            if (!DecodeOfferTx(acceptTx, op, nOut, offerVvch, -1) 
            	|| !IsOfferOp(op) 
            	|| (op != OP_OFFER_ACCEPT))
                continue;
			if(offerVvch[0] != vchOfferToFind && !vchOfferToFind.empty())
				continue;
			// check for existence of offeraccept in txn offer obj
			if(!theOffer.GetAcceptByHash(theEscrow.vchOfferAcceptLink, theOfferAccept))
				continue;	
			const vector<unsigned char> &vchAcceptRand = offerVvch[1];
			// get last active accept only
			if (vNamesI.find(vchAcceptRand) != vNamesI.end() && (theOfferAccept.nHeight <= vNamesI[vchAcceptRand] || vNamesI[vchAcceptRand] < 0))
				continue;
			vNamesI[vchAcceptRand] = theOfferAccept.nHeight;
			string offer = stringFromVch(vchOffer);
			string sHeight = strprintf("%llu", theOfferAccept.nHeight);
			oOfferAccept.push_back(Pair("offer", offer));
			oOfferAccept.push_back(Pair("title", stringFromVch(theOffer.sTitle)));
			oOfferAccept.push_back(Pair("id", stringFromVch(vchAcceptRand)));
			oOfferAccept.push_back(Pair("linkofferaccept", stringFromVch(theOfferAccept.vchLinkOfferAccept)));
			oOfferAccept.push_back(Pair("alias", theOffer.aliasName));
			oOfferAccept.push_back(Pair("buyerkey", stringFromVch(theOfferAccept.vchBuyerKey)));
			oOfferAccept.push_back(Pair("height", sHeight));
			oOfferAccept.push_back(Pair("quantity", strprintf("%u", theOfferAccept.nQty)));
			oOfferAccept.push_back(Pair("currency", stringFromVch(theOffer.sCurrencyCode)));
			oOfferAccept.push_back(Pair("escrowlink", stringFromVch(theOfferAccept.vchEscrowLink)));
			int precision = 2;
			convertCurrencyCodeToSyscoin(theOffer.sCurrencyCode, 0, chainActive.Tip()->nHeight, precision);
			oOfferAccept.push_back(Pair("price", strprintf("%.*f", precision, theOfferAccept.nPrice ))); 
			oOfferAccept.push_back(Pair("total", strprintf("%.*f", precision, theOfferAccept.nPrice * theOfferAccept.nQty ))); 
			// this accept is for me(something ive sold) if this offer is mine
			oOfferAccept.push_back(Pair("is_mine", IsOfferMine(offerTx)? "true" : "false"));
			if(theOfferAccept.bPaid && !theOfferAccept.bRefunded) {
				oOfferAccept.push_back(Pair("status","paid"));
			}
			else if(!theOfferAccept.bRefunded)
			{
				oOfferAccept.push_back(Pair("status","not paid"));
			}
			else if(theOfferAccept.bRefunded) { 
				oOfferAccept.push_back(Pair("status", "refunded"));
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
            if (!DecodeOfferTx(wtx, op, nOut, vvch, -1) 
            	|| !IsOfferOp(op) 
            	|| (op == OP_OFFER_ACCEPT))
                continue;

            // get the txn alias name
            vchName = vvch[0];

			// skip this offer if it doesn't match the given filter value
			if (vchNameUniq.size() > 0 && vchNameUniq != vchName)
				continue;
			// get last active name only
			if (vNamesI.find(vchName) != vNamesI.end() && (nHeight < vNamesI[vchName] || vNamesI[vchName] < 0))
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
			uint256 blockHash;
			if (!GetTransaction(theOfferA.txHash, tx, Params().GetConsensus(), blockHash, true))
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
			string strAddress = "";
            GetOfferAddress(tx, strAddress);
			CSyscoinAddress address(strAddress);
			if(address.IsValid() && address.isAlias)
				oName.push_back(Pair("address", address.aliasName));
			else
				oName.push_back(Pair("address", address.ToString()));
			oName.push_back(Pair("exclusive_resell", theOfferA.linkWhitelist.bExclusiveResell ? "ON" : "OFF"));
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
			oName.push_back(Pair("alias", theOfferA.aliasName));
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
		uint256 blockHash;
		BOOST_FOREACH(txPos2, vtxPos) {
			txHash = txPos2.txHash;
			CTransaction tx;
			if (!GetTransaction(txHash, tx, Params().GetConsensus(), blockHash, true)) {
				error("could not read txpos");
				continue;
			}
			int expired = 0;
			int expires_in = 0;
			int expired_block = 0;
			UniValue oOffer(UniValue::VOBJ);
			vector<unsigned char> vchValue;
			uint64_t nHeight;
			nHeight = txPos2.nHeight;
			oOffer.push_back(Pair("offer", offer));
			string value = stringFromVch(vchValue);
			oOffer.push_back(Pair("value", value));
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
			oOffer.push_back(Pair("alias", txPos2.aliasName));
			oOffer.push_back(Pair("expires_in", expires_in));
			oOffer.push_back(Pair("expires_on", expired_block));
			oOffer.push_back(Pair("expired", expired));
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
		string alias = txOffer.aliasName;
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
        uint256 blockHash;
		if (!GetTransaction(txOffer.txHash, tx, Params().GetConsensus(), blockHash, true))
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
		expired_block = nHeight + GetOfferExpirationDepth();
		if(nHeight + GetOfferExpirationDepth() - chainActive.Tip()->nHeight <= 0)
		{
			expired = 1;
		}  
		if(expired == 0)
		{
			expires_in = nHeight + GetOfferExpirationDepth() - chainActive.Tip()->nHeight;
		}
		oOffer.push_back(Pair("private", txOffer.bPrivate ? "Yes" : "No"));
		oOffer.push_back(Pair("alias", txOffer.aliasName));
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
		oOffer.push_back(Pair("alias", txOffer.aliasName));
		oOffer.push_back(Pair("expires_in", expires_in));
		oOffer.push_back(Pair("expires_on", expired_block));
		oOffer.push_back(Pair("expired", expired));
		oRes.push_back(oOffer);
	}

	return oRes;
}