#include "escrow.h"
#include "offer.h"
#include "alias.h"
#include "cert.h"
#include "init.h"
#include "main.h"
#include "util.h"
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
void PutToEscrowList(std::vector<CEscrow> &escrowList, CEscrow& index) {
	int i = escrowList.size() - 1;
	BOOST_REVERSE_FOREACH(CEscrow &o, escrowList) {
        if(index.nHeight != 0 && o.nHeight == index.nHeight) {
        	escrowList[i] = index;
            return;
        }
        else if(!o.txHash.IsNull() && o.txHash == index.txHash) {
        	escrowList[i] = index;
            return;
        }
        i--;
	}
    escrowList.push_back(index);
}
bool IsEscrowOp(int op) {
    return op == OP_ESCROW_ACTIVATE
        || op == OP_ESCROW_RELEASE
        || op == OP_ESCROW_REFUND
		|| op == OP_ESCROW_COMPLETE;
}
// 0.05% fee on escrow value for arbiter
int64_t GetEscrowArbiterFee(int64_t escrowValue) {

	int64_t nFee = escrowValue*0.005;
	if(nFee < DEFAULT_MIN_RELAY_TX_FEE)
		nFee = DEFAULT_MIN_RELAY_TX_FEE;
	return nFee;
}
// Increase expiration to 36000 gradually starting at block 24000.
// Use for validation purposes and pass the chain height.
int GetEscrowExpirationDepth() {
    return 525600;
}


string escrowFromOp(int op) {
    switch (op) {
    case OP_ESCROW_ACTIVATE:
        return "escrowactivate";
    case OP_ESCROW_RELEASE:
        return "escrowrelease";
    case OP_ESCROW_REFUND:
        return "escrowrefund";
	case OP_ESCROW_COMPLETE:
		return "escrowcomplete";
    default:
        return "<unknown escrow op>";
    }
}
bool CEscrow::UnserializeFromTx(const CTransaction &tx) {
	vector<unsigned char> vchData;
	if(!GetSyscoinData(tx, vchData))
		return false;
    try {
        CDataStream dsEscrow(vchData, SER_NETWORK, PROTOCOL_VERSION);
        dsEscrow >> *this;
    } catch (std::exception &e) {
        return false;
    }
    return true;
}
const vector<unsigned char> CEscrow::Serialize() {
    CDataStream dsEscrow(SER_NETWORK, PROTOCOL_VERSION);
    dsEscrow << *this;
    const vector<unsigned char> vchData(dsEscrow.begin(), dsEscrow.end());
    return vchData;

}
//TODO implement
bool CEscrowDB::ScanEscrows(const std::vector<unsigned char>& vchEscrow, unsigned int nMax,
        std::vector<std::pair<std::vector<unsigned char>, CEscrow> >& escrowScan) {

    CDBIterator *pcursor = pescrowdb->NewIterator();

    CDataStream ssKeySet(SER_DISK, CLIENT_VERSION);
    ssKeySet << make_pair(string("escrowi"), vchEscrow);
    pcursor->Seek(ssKeySet.str());

    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
		pair<string, vector<unsigned char> > key;
        try {        
            if (pcursor->GetKey(key) && key.first == "escrowi") {
                vector<unsigned char> vchEscrow = key.second;
                vector<CEscrow> vtxPos;
				pcursor->GetValue(vtxPos);
                CEscrow txPos;
                if (!vtxPos.empty())
                    txPos = vtxPos.back();
                escrowScan.push_back(make_pair(vchEscrow, txPos));
            }
            if (escrowScan.size() >= nMax)
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
 * [CEscrowDB::ReconstructEscrowIndex description]
 * @param  pindexRescan [description]
 * @return              [description]
 */
bool CEscrowDB::ReconstructEscrowIndex(CBlockIndex *pindexRescan) {
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

            // decode the escrow op, params, height
            bool o = DecodeEscrowTx(tx, op, nOut, vvchArgs, -1);
            if (!o || !IsEscrowOp(op)) continue;

            vector<unsigned char> vchEscrow = vvchArgs[0];

            // get the transaction
            if(!GetTransaction(tx.GetHash(), tx, Params().GetConsensus(), txblkhash, true))
                continue;

            // attempt to read escrow from txn
            CEscrow txEscrow;
            if(!txEscrow.UnserializeFromTx(tx))
                return error("ReconstructEscrowIndex() : failed to unserialize escrow from tx");

            // read escrow from DB if it exists
            vector<CEscrow> vtxPos;
            if (ExistsEscrow(vchEscrow)) {
                if (!ReadEscrow(vchEscrow, vtxPos))
                    return error("ReconstructEscrowIndex() : failed to read escrow from DB");
            }

            txEscrow.txHash = tx.GetHash();
            txEscrow.nHeight = nHeight;
            PutToEscrowList(vtxPos, txEscrow);

            if (!WriteEscrow(vchEscrow, vtxPos))
                return error("ReconstructEscrowIndex() : failed to write to escrow DB");

          
            LogPrintf( "RECONSTRUCT ESCROW: op=%s escrow=%s hash=%s height=%d\n",
                    escrowFromOp(op).c_str(),
                    stringFromVch(vvchArgs[0]).c_str(),
                    tx.GetHash().ToString().c_str(),
                    nHeight);
        }
        pindex = chainActive.Next(pindex);
        
    }
    }
    return true;
}

int IndexOfEscrowOutput(const CTransaction& tx) {
    vector<vector<unsigned char> > vvch;
    int op, nOut;
    if (!DecodeEscrowTx(tx, op, nOut, vvch, -1))
        throw runtime_error("IndexOfEscrowOutput() : escrow output not found");
    return nOut;
}

bool IsEscrowMine(const CTransaction& tx) {
    if (tx.nVersion != SYSCOIN_TX_VERSION)
        return false;

    vector<vector<unsigned char> > vvch;
    int op, nOut;

    bool good = DecodeEscrowTx(tx, op, nOut, vvch, -1);
    if (!good) 
        return false;
    
    if(!IsEscrowOp(op))
        return false;

    const CTxOut& txout = tx.vout[nOut];
   	if (pwalletMain->IsMine(txout)) {
        return true;
    }
    return false;
}


bool GetTxOfEscrow(CEscrowDB& dbEscrow, const vector<unsigned char> &vchEscrow,
        CEscrow& txPos, CTransaction& tx) {
    vector<CEscrow> vtxPos;
    if (!pescrowdb->ReadEscrow(vchEscrow, vtxPos) || vtxPos.empty())
        return false;
    txPos = vtxPos.back();
    int nHeight = txPos.nHeight;
    if (nHeight + GetEscrowExpirationDepth()
            < chainActive.Tip()->nHeight) {
        string escrow = stringFromVch(vchEscrow);
        LogPrintf("GetTxOfEscrow(%s) : expired", escrow.c_str());
        return false;
    }

    uint256 hashBlock;
    if (!GetTransaction(txPos.txHash, tx, Params().GetConsensus(), hashBlock, true))
        return error("GetTxOfEscrow() : could not read tx from disk");

    return true;
}

bool DecodeEscrowTx(const CTransaction& tx, int& op, int& nOut,
        vector<vector<unsigned char> >& vvch, int nHeight) {
    bool found = false;


    // Strict check - bug disallowed
    for (unsigned int i = 0; i < tx.vout.size(); i++) {
        const CTxOut& out = tx.vout[i];
        vector<vector<unsigned char> > vvchRead;
        if (DecodeEscrowScript(out.scriptPubKey, op, vvchRead)) {
            nOut = i; found = true; vvch = vvchRead;
            break;
        }
    }
	if (!found) vvch.clear();
    return found && IsEscrowOp(op);
}

bool DecodeEscrowScript(const CScript& script, int& op,
        vector<vector<unsigned char> > &vvch) {
    CScript::const_iterator pc = script.begin();
    return DecodeEscrowScript(script, op, vvch, pc);
}

bool DecodeEscrowScript(const CScript& script, int& op,
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

    if ((op == OP_ESCROW_ACTIVATE && vvch.size() == 1)
        || (op == OP_ESCROW_RELEASE && vvch.size() == 2)
        || (op == OP_ESCROW_REFUND && vvch.size() == 2)
		|| (op == OP_ESCROW_COMPLETE && vvch.size() == 2))
        return true;

    return false;
}

bool GetEscrowAddress(const CTransaction& tx, std::string& strAddress) {
    int op, nOut = 0;
    vector<vector<unsigned char> > vvch;
    if (!DecodeEscrowTx(tx, op, nOut, vvch, -1))
        return error("GetEscrowAddress() : could not decode escrow tx.");

    const CTxOut& txout = tx.vout[nOut];

    const CScript& scriptPubKey = RemoveEscrowScriptPrefix(txout.scriptPubKey);
	CTxDestination dest;
	ExtractDestination(scriptPubKey, dest);
	strAddress = CSyscoinAddress(dest).ToString();
    return true;
}


CScript RemoveEscrowScriptPrefix(const CScript& scriptIn) {
    int op;
    vector<vector<unsigned char> > vvch;
    CScript::const_iterator pc = scriptIn.begin();

    if (!DecodeEscrowScript(scriptIn, op, vvch, pc))
	{
        throw runtime_error("RemoveEscrowScriptPrefix() : could not decode escrow script");
	}
	
    return CScript(pc, scriptIn.end());
}

bool CheckEscrowInputs(const CTransaction &tx,
        CValidationState &state, const CCoinsViewCache &inputs, bool fBlock, bool fMiner,
        bool fJustCheck, int nHeight) {

    if (!tx.IsCoinBase()) {
			LogPrintf("*** %d %d %s %s %s %s\n", nHeight,
				chainActive.Tip()->nHeight, tx.GetHash().ToString().c_str(),
				fBlock ? "BLOCK" : "", fMiner ? "MINER" : "",
				fJustCheck ? "JUSTCHECK" : "");

        bool found = false;
        const COutPoint *prevOutput = NULL;
        CCoins prevCoins;

        int prevOp;
        vector<vector<unsigned char> > vvchPrevArgs;
		vvchPrevArgs.clear();
        // Strict check - bug disallowed
		for (unsigned int i = 0; i < tx.vin.size(); i++) {
			vector<vector<unsigned char> > vvch;
			prevOutput = &tx.vin[i].prevout;
			inputs.GetCoins(prevOutput->hash, prevCoins);
			if(DecodeEscrowScript(prevCoins.vout[prevOutput->n].scriptPubKey, prevOp, vvch))
			{
				vvchPrevArgs = vvch;
				found = true;
				break;
			}
			if(!found)vvchPrevArgs.clear();
			
		}
		
        // Make sure escrow outputs are not spent by a regular transaction, or the escrow would be lost
        if (tx.nVersion != SYSCOIN_TX_VERSION) {
            if (found)
                return error(
                        "CheckEscrowInputs() : a non-syscoin transaction with a syscoin input");
			LogPrintf("CheckEscrowInputs() : non-syscoin transaction\n");
            return true;
        }
        vector<vector<unsigned char> > vvchArgs;
        int op, nOut;
        bool good = DecodeEscrowTx(tx, op, nOut, vvchArgs, -1);
        if (!good)
            return error("CheckEscrowInputs() : could not decode a syscoin tx");
        // unserialize escrow UniValue from txn, check for valid
        CEscrow theEscrow;
        theEscrow.UnserializeFromTx(tx);
        if (theEscrow.IsNull())
            return error("CheckEscrowInputs() : null escrow");
        if (vvchArgs[0].size() > MAX_NAME_LENGTH)
            return error("escrow tx GUID too big");
        switch (op) {
			case OP_ESCROW_ACTIVATE:
				break;
			case OP_ESCROW_RELEASE:
			case OP_ESCROW_COMPLETE:
			case OP_ESCROW_REFUND:
				if (vvchArgs[1].size() > MAX_NAME_LENGTH)
					return error("escrow tx with offer GUID too long");
				break;
			default:
				return error( "CheckEscrowInputs() : escrow transaction has unknown op");
        }
        // these ifs are problably total bullshit except for the escrownew
        if (fBlock || (!fBlock && !fMiner && !fJustCheck)) {
			// save serialized escrow for later use
			CEscrow serializedEscrow = theEscrow;

			// if not an escrownew, load the escrow data from the DB
			vector<CEscrow> vtxPos;
			if (pescrowdb->ExistsEscrow(vvchArgs[0]) && !fJustCheck) {
				if (!pescrowdb->ReadEscrow(vvchArgs[0], vtxPos))
					return error(
							"CheckEscrowInputs() : failed to read from escrow DB");
			}
            if (!fMiner && !fJustCheck && chainActive.Tip()->nHeight != nHeight) {
                int nHeight = chainActive.Tip()->nHeight;
				// make sure escrow settings don't change (besides rawTx) outside of activation
				if(op != OP_ESCROW_ACTIVATE) 
				{
					bool escrowChanged = false;
					// make sure we have found this escrow in db
					if(!vtxPos.empty())
					{
						theEscrow = vtxPos.back();
						// these are the only settings allowed to change outside of activate
						if(!serializedEscrow.rawTx.empty())
							theEscrow.rawTx = serializedEscrow.rawTx;
						if(!serializedEscrow.vchOfferAcceptLink.empty())
							theEscrow.vchOfferAcceptLink = serializedEscrow.vchOfferAcceptLink;
					}
				}
				

                // set the escrow's txn-dependent values
				theEscrow.txHash = tx.GetHash();
				theEscrow.nHeight = nHeight;
				PutToEscrowList(vtxPos, theEscrow);
				{
				TRY_LOCK(cs_main, cs_trymain);
                // write escrow  
                if (!pescrowdb->WriteEscrow(vvchArgs[0], vtxPos))
                    return error( "CheckEscrowInputs() : failed to write to escrow DB");

              			
                // debug
				if(fDebug)
					LogPrintf( "CONNECTED ESCROW: op=%s escrow=%s hash=%s height=%d\n",
                        escrowFromOp(op).c_str(),
                        stringFromVch(vvchArgs[0]).c_str(),
                        tx.GetHash().ToString().c_str(),
                        nHeight);
				}
            }
            
        }
    }
    return true;
}

void rescanforescrows(CBlockIndex *pindexRescan) {
    LogPrintf("Scanning blockchain for escrows to create fast index...\n");
    pescrowdb->ReconstructEscrowIndex(pindexRescan);
}



UniValue escrownew(const UniValue& params, bool fHelp) {
    if (fHelp || params.size() != 4 )
        throw runtime_error(
		"escrownew <offer> <quantity> <message> <arbiter alias>\n"
                        "<offer> GUID of offer that this escrow is managing.\n"
                        "<quantity> Quantity of items to buy of offer.\n"
						"<message> Delivery details to seller.\n"
						"<arbiter alias> Alias of Arbiter.\n"
                        + HelpRequiringPassphrase());
	if(!HasReachedMainNetForkB2())
		throw runtime_error("Please wait until B2 hardfork starts in before executing this command.");
	vector<unsigned char> vchOffer = vchFromValue(params[0]);
	string strArbiter = params[3].get_str();
	CSyscoinAddress arbiterAddress = CSyscoinAddress(strArbiter);
	if (!arbiterAddress.IsValid())
		throw runtime_error("Invalid arbiter syscoin address");
	if (!arbiterAddress.isAlias)
		throw runtime_error("Arbiter must be a valid alias");
	if(IsMine(*pwalletMain, arbiterAddress.Get()))
		throw runtime_error("Arbiter alias must not be yours");
	// check for alias existence in DB
	vector<CAliasIndex> vtxPos;
	if (!paliasdb->ReadAlias(vchFromString(arbiterAddress.aliasName), vtxPos))
		throw runtime_error("failed to read alias from alias DB");
	if (vtxPos.size() < 1)
		throw runtime_error("no result returned");
	CAliasIndex xferAlias = vtxPos.back();
	std::vector<unsigned char> vchArbiterPubKey = xferAlias.vchPubKey;

	vector<unsigned char> vchMessage = vchFromValue(params[2]);
	unsigned int nQty = 1;
	if(atof(params[1].get_str().c_str()) < 0)
		throw runtime_error("invalid quantity value, must be greator than 0");

	try {
		nQty = boost::lexical_cast<unsigned int>(params[1].get_str());
	} catch (std::exception &e) {
		throw runtime_error("invalid quantity value. Quantity must be less than 4294967296.");
	}

    if (vchMessage.size() <= 0)
        vchMessage = vchFromString("ESCROW");

	COffer theOffer;
	CTransaction txOffer;
	if (!GetTxOfOffer(*pofferdb, vchOffer, theOffer, txOffer))
		throw runtime_error("could not find an offer with this identifier");

	if (ExistsInMempool(vchOffer, OP_OFFER_REFUND) || ExistsInMempool(vchOffer, OP_OFFER_ACTIVATE) || ExistsInMempool(vchOffer, OP_OFFER_UPDATE)) {
		throw runtime_error("there are pending operations or refunds on that offer");
	}
	
    // gather inputs
	int64_t rand = GetRand(std::numeric_limits<int64_t>::max());
	vector<unsigned char> vchRand = CScriptNum(rand).getvch();
    vector<unsigned char> vchEscrow = vchFromValue(HexStr(vchRand));

    // this is a syscoin transaction
    CWalletTx wtx;
	EnsureWalletIsUnlocked();
    //create escrowactivate txn keys
    CPubKey newDefaultKey;
    pwalletMain->GetKeyFromPool(newDefaultKey);
    CScript scriptPubKey,scriptPubKeySeller,scriptSeller, scriptPubKeyArbiter, scriptArbiter;

	std::vector<unsigned char> vchSellerKeyByte;
    boost::algorithm::unhex(theOffer.vchPubKey.begin(), theOffer.vchPubKey.end(), std::back_inserter(vchSellerKeyByte));
	CPubKey SellerPubKey(vchSellerKeyByte);
	CSyscoinAddress selleraddy(SellerPubKey.GetID());
	string strCipherText = "";
	// encrypt to offer owner
	if(!EncryptMessage(theOffer.vchPubKey, vchMessage, strCipherText))
		throw runtime_error("could not encrypt message to seller");
	
	if (strCipherText.size() > MAX_ENCRYPTED_VALUE_LENGTH)
		throw runtime_error("offeraccept message length cannot exceed 1023 bytes!");

	std::vector<unsigned char> vchArbiterKeyByte;
    boost::algorithm::unhex(vchArbiterPubKey.begin(), vchArbiterPubKey.end(), std::back_inserter(vchArbiterKeyByte));
	CPubKey ArbiterPubKey(vchArbiterKeyByte);
	CSyscoinAddress arbaddy(ArbiterPubKey.GetID());


	std::vector<unsigned char> vchBuyerKey(newDefaultKey.begin(), newDefaultKey.end());
	string strBuyerKey = HexStr(vchBuyerKey);

	scriptArbiter= GetScriptForDestination(ArbiterPubKey.GetID());
	scriptSeller= GetScriptForDestination(SellerPubKey.GetID());
	scriptPubKeySeller << CScript::EncodeOP_N(OP_ESCROW_ACTIVATE) << vchEscrow << OP_2DROP;
	scriptPubKeySeller += scriptSeller;
	scriptPubKeyArbiter << CScript::EncodeOP_N(OP_ESCROW_ACTIVATE) << vchEscrow << OP_2DROP;
	scriptPubKeyArbiter += scriptArbiter;

	UniValue arrayParams(UniValue::VARR);
	UniValue arrayOfKeys(UniValue::VARR);

	// standard 2 of 3 multisig
	arrayParams.push_back(2);
	arrayOfKeys.push_back(stringFromVch(vchArbiterPubKey));
	arrayOfKeys.push_back(stringFromVch(theOffer.vchPubKey));
	arrayOfKeys.push_back(strBuyerKey);
	arrayParams.push_back(arrayOfKeys);
	UniValue resCreate;
	try
	{
		resCreate = tableRPC.execute("createmultisig", arrayParams);
	}
	catch (UniValue& objError)
	{
		throw runtime_error(find_value(objError, "message").get_str());
	}
	if (!resCreate.isObject())
		throw runtime_error("Could not create escrow transaction: Invalid response from createescrow!");
	const UniValue &o = resCreate.get_obj();
	string redeemScript_str = "";
	const UniValue& redeemScript_value = find_value(o, "redeemScript");
	if (redeemScript_value.isStr())
	{
		redeemScript_str = redeemScript_value.get_str();
		vector<unsigned char> rsData(ParseHex(redeemScript_str));
		scriptPubKey = CScript(rsData.begin(), rsData.end());
	}
	// send to escrow address

	int precision = 2;
	int64_t nPricePerUnit = convertCurrencyCodeToSyscoin(theOffer.sCurrencyCode, theOffer.GetPrice(), chainActive.Tip()->nHeight, precision);
	int64_t nTotal = nPricePerUnit*nQty;

	int64_t nEscrowFee = GetEscrowArbiterFee(nTotal);
	int64_t nAmountWithEscrowFee = nTotal+nEscrowFee;

	CWalletTx escrowWtx;
	vector<CRecipient> vecSendEscrow;
	CRecipient recipientEscrow  = {scriptPubKey, nAmountWithEscrowFee, false};
	vecSendEscrow.push_back(recipientEscrow);
	SendMoneySyscoin(vecSendEscrow, nAmountWithEscrowFee, false, escrowWtx, NULL, false);
	
	// send to seller/arbiter so they can track the escrow through GUI
    // build escrow
    CEscrow newEscrow;
	newEscrow.vchBuyerKey = vchFromString(strBuyerKey);
	newEscrow.seller = theOffer.aliasName;
	newEscrow.arbiter = strArbiter;
	newEscrow.vchArbiterKey = vchArbiterPubKey;
	newEscrow.vchRedeemScript = vchFromString(redeemScript_str);
	newEscrow.vchOffer = vchOffer;
	newEscrow.vchSellerKey = theOffer.vchPubKey;
	newEscrow.vchPaymentMessage = vchFromString(strCipherText);
	newEscrow.nQty = nQty;
	newEscrow.escrowInputTxHash = escrowWtx.GetHash();
	newEscrow.nPricePerUnit = nPricePerUnit;
	newEscrow.nHeight = chainActive.Tip()->nHeight;
	// send the tranasction
	vector<CRecipient> vecSend;
	CRecipient recipientSeller;
	CreateRecipient(scriptPubKeySeller, recipientSeller);
	vecSend.push_back(recipientSeller);

	const vector<unsigned char> &data = newEscrow.Serialize();
	CScript scriptData;
	scriptData << OP_RETURN << data;
	CRecipient fee;
	CreateFeeRecipient(scriptData, data, fee);
	vecSend.push_back(fee);

	SendMoneySyscoin(vecSend, recipientSeller.nAmount+fee.nAmount, false, wtx);

	vecSend.clear();
	CRecipient recipientArbiter;
	CreateRecipient(scriptPubKeyArbiter, recipientArbiter);
	vecSend.push_back(recipientArbiter);
	vecSend.push_back(fee);
	SendMoneySyscoin(vecSend, recipientArbiter.nAmount+fee.nAmount, false, wtx);
	UniValue res(UniValue::VARR);
	res.push_back(wtx.GetHash().GetHex());
	res.push_back(HexStr(vchRand));
	return res;
}
UniValue escrowrelease(const UniValue& params, bool fHelp) {
    if (fHelp || params.size() != 1)
        throw runtime_error(
		"escrowrelease <escrow guid>\n"
                        "Releases escrow funds to seller, seller needs to sign the output transaction and send to the network.\n"
                        + HelpRequiringPassphrase());
	if(!HasReachedMainNetForkB2())
		throw runtime_error("Please wait until B2 hardfork starts in before executing this command.");
    // gather & validate inputs
    vector<unsigned char> vchEscrow = vchFromValue(params[0]);

    // this is a syscoin transaction
    CWalletTx wtx;

	EnsureWalletIsUnlocked();

    // look for a transaction with this key
    CTransaction tx;
	CEscrow escrow;
    if (!GetTxOfEscrow(*pescrowdb, vchEscrow, 
		escrow, tx))
        throw runtime_error("could not find a escrow with this key");
    vector<vector<unsigned char> > vvch;
    int op, nOut;
    if (!DecodeEscrowTx(tx, op, nOut, vvch, -1) 
    	|| !IsEscrowOp(op) 
    	|| (op != OP_ESCROW_ACTIVATE))
        throw runtime_error("Release can only happen on an activated escrow address");

    // unserialize escrow UniValue from txn
    CEscrow theEscrow;
    if(!theEscrow.UnserializeFromTx(tx))
        throw runtime_error("cannot unserialize escrow from txn");
    CTransaction fundingTx;
	uint256 blockHash;
	if (!GetTransaction(escrow.escrowInputTxHash, fundingTx, Params().GetConsensus(), blockHash, true))
		throw runtime_error("failed to escrow transaction");

	std::vector<unsigned char> vchArbiterKeyByte;
    boost::algorithm::unhex(escrow.vchArbiterKey.begin(), escrow.vchArbiterKey.end(), std::back_inserter(vchArbiterKeyByte));
	CPubKey arbiterKey(vchArbiterKeyByte);
	CSyscoinAddress arbiterAddress(arbiterKey.GetID());
	if(!arbiterAddress.IsValid())
		throw runtime_error("Arbiter address is invalid!");

	std::vector<unsigned char> vchBuyerKeyByte;
    boost::algorithm::unhex(escrow.vchBuyerKey.begin(), escrow.vchBuyerKey.end(), std::back_inserter(vchBuyerKeyByte));
	CPubKey buyerKey(vchBuyerKeyByte);
	CSyscoinAddress buyerAddress(buyerKey.GetID());
	if(!buyerAddress.IsValid())
		throw runtime_error("Buyer address is invalid!");

	
	std::vector<unsigned char> vchSellerKeyByte;
    boost::algorithm::unhex(escrow.vchSellerKey.begin(), escrow.vchSellerKey.end(), std::back_inserter(vchSellerKeyByte));
	CPubKey sellerKey(vchSellerKeyByte);
	CSyscoinAddress sellerAddress(sellerKey.GetID());
	if(!sellerAddress.IsValid())
		throw runtime_error("Seller address is invalid!");
	int nOutMultiSig = 0;
	int64_t nExpectedAmount = escrow.nPricePerUnit*escrow.nQty;
	int64_t nEscrowFee = GetEscrowArbiterFee(nExpectedAmount);
	int64_t nExpectedAmountWithEscrowFee = nExpectedAmount+nEscrowFee;
	for(unsigned int i=0;i<fundingTx.vout.size();i++)
	{
		if(fundingTx.vout[i].nValue == nExpectedAmountWithEscrowFee)
		{
			nOutMultiSig = i;
			break;
		}
	} 
	int64_t nAmount = fundingTx.vout[nOutMultiSig].nValue;
	string strEscrowScriptPubKey = HexStr(fundingTx.vout[nOutMultiSig].scriptPubKey.begin(), fundingTx.vout[nOutMultiSig].scriptPubKey.end());
	if(nAmount != nExpectedAmountWithEscrowFee)
		throw runtime_error("Expected amount of escrow does not match what is held in escrow!");

	string strPrivateKey ;
	bool arbiterSigning = false;
	// who is initiating release arbiter or buyer?
	try
	{
		arbiterSigning = true;
		// try arbiter
		CKeyID keyID;
		if (!arbiterAddress.GetKeyID(keyID))
			throw runtime_error("Arbiter address does not refer to a key");
		CKey vchSecret;
		if (!pwalletMain->GetKey(keyID, vchSecret))
			throw runtime_error("Private key for arbiter address " + arbiterAddress.ToString() + " is not known");
		strPrivateKey = CSyscoinSecret(vchSecret).ToString();
	}
	catch(...)
	{
		arbiterSigning = false;
		// otherwise try buyer
		CKeyID keyID;
		if (!buyerAddress.GetKeyID(keyID))
			throw runtime_error("Buyer or Arbiter address does not refer to a key");
		CKey vchSecret;
		if (!pwalletMain->GetKey(keyID, vchSecret))
			throw runtime_error("Buyer or Arbiter private keys not known");
		strPrivateKey = CSyscoinSecret(vchSecret).ToString();
	}
     	// check for existing escrow 's
	if (ExistsInMempool(vchEscrow, OP_ESCROW_ACTIVATE) || ExistsInMempool(vchEscrow, OP_ESCROW_RELEASE) || ExistsInMempool(vchEscrow, OP_ESCROW_REFUND) || ExistsInMempool(vchEscrow, OP_ESCROW_COMPLETE)) {
		throw runtime_error("there are pending operations on that escrow");
	}
	// create a raw tx that sends escrow amount to seller and collateral to buyer
    // inputs buyer txHash
	UniValue arrayCreateParams(UniValue::VARR);
	UniValue createTxInputsArray(UniValue::VARR);
	UniValue createTxInputUniValue(UniValue::VOBJ);
	UniValue createAddressUniValue(UniValue::VOBJ);
	createTxInputUniValue.push_back(Pair("txid", escrow.escrowInputTxHash.ToString()));
	createTxInputUniValue.push_back(Pair("vout", nOutMultiSig));
	createTxInputsArray.push_back(createTxInputUniValue);
	if(arbiterSigning)
	{
		createAddressUniValue.push_back(Pair(sellerAddress.ToString(), ValueFromAmount(nExpectedAmount)));
		createAddressUniValue.push_back(Pair(arbiterAddress.ToString(), ValueFromAmount(nEscrowFee)));
	}
	else
	{
		createAddressUniValue.push_back(Pair(sellerAddress.ToString(), ValueFromAmount(nExpectedAmount)));
		createAddressUniValue.push_back(Pair(buyerAddress.ToString(), ValueFromAmount(nEscrowFee)));
	}

	arrayCreateParams.push_back(createTxInputsArray);
	arrayCreateParams.push_back(createAddressUniValue);
	UniValue resCreate;
	try
	{
		resCreate = tableRPC.execute("createrawtransaction", arrayCreateParams);
	}
	catch (UniValue& objError)
	{
		throw runtime_error(find_value(objError, "message").get_str());
	}	
	if (!resCreate.isStr())
		throw runtime_error("Could not create escrow transaction: Invalid response from createrawtransaction!");
	string createEscrowSpendingTx = resCreate.get_str();

	// Buyer/Arbiter signs it
	UniValue arraySignParams(UniValue::VARR);
	UniValue arraySignInputs(UniValue::VARR);
	UniValue arrayPrivateKeys(UniValue::VARR);

	UniValue signUniValue(UniValue::VOBJ);
	signUniValue.push_back(Pair("txid", escrow.escrowInputTxHash.ToString()));
	signUniValue.push_back(Pair("vout", nOutMultiSig));
	signUniValue.push_back(Pair("scriptPubKey", strEscrowScriptPubKey));
	signUniValue.push_back(Pair("redeemScript", stringFromVch(escrow.vchRedeemScript)));
	arraySignParams.push_back(createEscrowSpendingTx);
	arraySignInputs.push_back(signUniValue);
	arraySignParams.push_back(arraySignInputs);
	arrayPrivateKeys.push_back(strPrivateKey);
	arraySignParams.push_back(arrayPrivateKeys);
	UniValue res;
	try
	{
		res = tableRPC.execute("signrawtransaction", arraySignParams);
	}
	catch (UniValue& objError)
	{
		throw runtime_error(find_value(objError, "message").get_str());
	}	
	if (!res.isObject())
		throw runtime_error("Could not sign escrow transaction: Invalid response from signrawtransaction!");
	
	const UniValue& o = res.get_obj();
	string hex_str = "";

	const UniValue& hex_value = find_value(o, "hex");
	if (hex_value.isStr())
		hex_str = hex_value.get_str();
	const UniValue& complete_value = find_value(o, "complete");
	bool bComplete = false;
	if (complete_value.isBool())
		bComplete = complete_value.get_bool();

	if(bComplete)
		throw runtime_error("This is not a multisignature escrow!");
	escrow.ClearEscrow();
	escrow.rawTx = vchFromString(hex_str);
	escrow.nHeight = chainActive.Tip()->nHeight;

    CScript scriptPubKey, scriptPubKeySeller;
	scriptPubKeySeller= GetScriptForDestination(sellerKey.GetID());
    scriptPubKey << CScript::EncodeOP_N(OP_ESCROW_RELEASE) << vchEscrow << escrow.vchOffer << OP_2DROP << OP_DROP;
    scriptPubKey += scriptPubKeySeller;

	vector<CRecipient> vecSend;
	CRecipient recipient;
	CreateRecipient(scriptPubKey, recipient);
	vecSend.push_back(recipient);

	const vector<unsigned char> &data = escrow.Serialize();
	CScript scriptData;
	scriptData << OP_RETURN << data;
	CRecipient fee;
	CreateFeeRecipient(scriptData, data, fee);
	vecSend.push_back(fee);

	SendMoneySyscoin(vecSend, recipient.nAmount+fee.nAmount, false, wtx);
	UniValue ret(UniValue::VARR);
	ret.push_back(wtx.GetHash().GetHex());
	return ret;
}
UniValue escrowclaimrelease(const UniValue& params, bool fHelp) {
    if (fHelp || params.size() != 1)
        throw runtime_error(
		"escrowclaimrelease <escrow guid>\n"
                        "Claim escrow funds released from buyer or arbiter using escrowrelease.\n"
                        + HelpRequiringPassphrase());
	if(!HasReachedMainNetForkB2())
		throw runtime_error("Please wait until B2 hardfork starts in before executing this command.");
    // gather & validate inputs
    vector<unsigned char> vchEscrow = vchFromValue(params[0]);


	EnsureWalletIsUnlocked();
    // look for a transaction with this key
    CTransaction tx;
	CEscrow escrow;
    if (!GetTxOfEscrow(*pescrowdb, vchEscrow, 
		escrow, tx))
        throw runtime_error("could not find a escrow with this key");
	CTransaction fundingTx;
	uint256 blockHash;
	if (!GetTransaction(escrow.escrowInputTxHash, fundingTx, Params().GetConsensus(), blockHash, true))
		throw runtime_error("failed to read escrow transaction");

 	int nOutMultiSig = 0;
	int64_t nExpectedAmount = escrow.nPricePerUnit*escrow.nQty;
	int64_t nEscrowFee = GetEscrowArbiterFee(nExpectedAmount);
	int64_t nExpectedAmountWithEscrowFee = nExpectedAmount+nEscrowFee;
	for(unsigned int i=0;i<fundingTx.vout.size();i++)
	{
		if(fundingTx.vout[i].nValue == nExpectedAmountWithEscrowFee)
		{
			nOutMultiSig = i;
			break;
		}
	} 
	int64_t nAmount = fundingTx.vout[nOutMultiSig].nValue;
	string strEscrowScriptPubKey = HexStr(fundingTx.vout[nOutMultiSig].scriptPubKey.begin(), fundingTx.vout[nOutMultiSig].scriptPubKey.end());
	if(nAmount != nExpectedAmountWithEscrowFee)
		throw runtime_error("Expected amount of escrow does not match what is held in escrow!");
	// decode rawTx and check it pays enough and it pays to buyer/seller appropriately
	// check that right amount is going to be sent to seller
	bool foundSellerPayment = false;
	bool foundFeePayment = false;
	UniValue arrayDecodeParams(UniValue::VARR);
	arrayDecodeParams.push_back(stringFromVch(escrow.rawTx));
	UniValue decodeRes;
	try
	{
		decodeRes = tableRPC.execute("decoderawtransaction", arrayDecodeParams);
	}
	catch (UniValue& objError)
	{
		throw runtime_error(find_value(objError, "message").get_str());
	}
	if (!decodeRes.isObject())
		throw runtime_error("Could not decode escrow transaction: Invalid response from decoderawtransaction!");
	const UniValue& decodeo = decodeRes.get_obj();
	const UniValue& vout_value = find_value(decodeo, "vout");
	if (!vout_value.isArray())
		throw runtime_error("Could not decode escrow transaction: Can't find vout's from transaction!");	
	const UniValue &vouts = vout_value.get_array();
    for (unsigned int idx = 0; idx < vouts.size(); idx++) {
        const UniValue& vout = vouts[idx];					
		const UniValue &voutObj = vout.get_obj();					
		const UniValue &voutValue = find_value(voutObj, "value");
		if(!voutValue.isNum())
			throw runtime_error("Could not decode escrow transaction: Invalid vout value!");
		int64_t iVout = AmountFromValue(voutValue);
		UniValue scriptPubKeyValue = find_value(voutObj, "scriptPubKey");
		if(!scriptPubKeyValue.isObject())
			throw runtime_error("Could not decode escrow transaction: Invalid scriptPubKey UniValue!");
		const UniValue &scriptPubKeyValueObj = scriptPubKeyValue.get_obj();	
		const UniValue &addressesValue = find_value(scriptPubKeyValueObj, "addresses");
		if(!addressesValue.isArray())
			throw runtime_error("Could not decode escrow transaction: Invalid addresses UniValue!");

		const UniValue &addresses = addressesValue.get_array();
		for (unsigned int idx = 0; idx < addresses.size(); idx++) {
			const UniValue& address = addresses[idx];
			if(!address.isStr())
				throw runtime_error("Could not decode escrow transaction: Invalid address UniValue!");
			const string &strAddress = address.get_str();
			CSyscoinAddress payoutAddress(strAddress);
			// check arb fee is paid to arbiter or buyer
			if(!foundFeePayment)
			{
				std::vector<unsigned char> vchArbiterKeyByte;
				boost::algorithm::unhex(escrow.vchArbiterKey.begin(), escrow.vchArbiterKey.end(), std::back_inserter(vchArbiterKeyByte));
				CPubKey arbiterKey(vchArbiterKeyByte);
				CSyscoinAddress arbiterAddress(arbiterKey.GetID());
				if(arbiterAddress == payoutAddress && iVout == nEscrowFee)
					foundFeePayment = true;
			}
			if(!foundFeePayment)
			{
				std::vector<unsigned char> vchBuyerKeyByte;
				boost::algorithm::unhex(escrow.vchBuyerKey.begin(), escrow.vchBuyerKey.end(), std::back_inserter(vchBuyerKeyByte));
				CPubKey buyerKey(vchBuyerKeyByte);
				CSyscoinAddress buyerAddress(buyerKey.GetID());
				if(buyerAddress == payoutAddress && iVout == nEscrowFee)
					foundFeePayment = true;
			}	
			if(IsMine(*pwalletMain, payoutAddress.Get()))
			{
				if(!foundSellerPayment)
				{
					if(iVout == nExpectedAmount)
					{
						foundSellerPayment = true;
					}
				}
			}
		}
	}


	CKeyID keyID;
	std::vector<unsigned char> vchSellerKeyByte;
    boost::algorithm::unhex(escrow.vchSellerKey.begin(), escrow.vchSellerKey.end(), std::back_inserter(vchSellerKeyByte));
	CPubKey sellerKey(vchSellerKeyByte);
	CSyscoinAddress sellerAddress(sellerKey.GetID());
	if(!sellerAddress.IsValid())
		throw runtime_error("Seller address is invalid!");

	if (!sellerAddress.GetKeyID(keyID))
		throw runtime_error("Seller address does not refer to a key");
	CKey vchSecret;
	if (!pwalletMain->GetKey(keyID, vchSecret))
		throw runtime_error("Private key for seller address " + sellerAddress.ToString() + " is not known");
	const string &strPrivateKey = CSyscoinSecret(vchSecret).ToString();
	if(!foundSellerPayment)
		throw runtime_error("Expected payment amount from escrow does not match what was expected by the seller!");	
	if(!foundFeePayment)    
		throw runtime_error("Expected fee payment to arbiter or buyer from escrow does not match what was expected!");	
	// check for existing escrow 's
	if (ExistsInMempool(vchEscrow, OP_ESCROW_ACTIVATE) || ExistsInMempool(vchEscrow, OP_ESCROW_RELEASE) || ExistsInMempool(vchEscrow, OP_ESCROW_REFUND) || ExistsInMempool(vchEscrow, OP_ESCROW_COMPLETE)) {
		throw runtime_error("there are pending operations on that escrow");
	}
    // Seller signs it
	UniValue arraySignParams(UniValue::VARR);
	UniValue arraySignInputs(UniValue::VARR);
	UniValue arrayPrivateKeys(UniValue::VARR);
	UniValue signUniValue(UniValue::VOBJ);
	signUniValue.push_back(Pair("txid", escrow.escrowInputTxHash.ToString()));
	signUniValue.push_back(Pair("vout", nOutMultiSig));
	signUniValue.push_back(Pair("scriptPubKey", strEscrowScriptPubKey));
	signUniValue.push_back(Pair("redeemScript", stringFromVch(escrow.vchRedeemScript)));
	arraySignParams.push_back(stringFromVch(escrow.rawTx));
	arraySignInputs.push_back(signUniValue);
	arraySignParams.push_back(arraySignInputs);
	arrayPrivateKeys.push_back(strPrivateKey);
	arraySignParams.push_back(arrayPrivateKeys);
	UniValue res;
	try
	{
		res = tableRPC.execute("signrawtransaction", arraySignParams);
	}
	catch (UniValue& objError)
	{
		throw runtime_error(find_value(objError, "message").get_str());
	}	
	if (!res.isObject())
		throw runtime_error("Could not sign escrow transaction: Invalid response from signrawtransaction!");
	
	const UniValue& o = res.get_obj();
	string hex_str = "";

	const UniValue& hex_value = find_value(o, "hex");
	if (hex_value.isStr())
		hex_str = hex_value.get_str();

	const UniValue& complete_value = find_value(o, "complete");
	bool bComplete = false;
	if (complete_value.isBool())
		bComplete = complete_value.get_bool();

	if(!bComplete)
		throw runtime_error("Could not sign escrow transaction. It is showing as incomplete, you may not allowed to complete this request at this time.");

	// broadcast the payment transaction
	UniValue arraySendParams(UniValue::VARR);
	arraySendParams.push_back(hex_str);
	try
	{
		res = tableRPC.execute("sendrawtransaction", arraySendParams);
	}
	catch (UniValue& objError)
	{
		throw runtime_error(find_value(objError, "message").get_str());
	}
	if (!res.isStr())
		throw runtime_error("Could not send escrow transaction: Invalid response from sendrawtransaction!");


	UniValue arrayAcceptParams(UniValue::VARR);
	arrayAcceptParams.push_back(stringFromVch(vchEscrow));
	try
	{
		res = tableRPC.execute("escrowcomplete", arrayAcceptParams);
	}
	catch (UniValue& objError)
	{
		throw runtime_error(find_value(objError, "message").get_str());
	}
	if (!res.isArray())
		throw runtime_error("Could not complete escrow: Invalid response from escrowcomplete!");
	return res;

	
	
}
UniValue escrowcomplete(const UniValue& params, bool fHelp) {
    if (fHelp || params.size() != 1)
        throw runtime_error(
		"escrowcomplete <escrow guid>\n"
                         "Accepts an offer that's in escrow, to complete the escrow process.\n"
                        + HelpRequiringPassphrase());
	if(!HasReachedMainNetForkB2())
		throw runtime_error("Please wait until B2 hardfork starts in before executing this command.");
    // gather & validate inputs
    vector<unsigned char> vchEscrow = vchFromValue(params[0]);

	EnsureWalletIsUnlocked();

    // look for a transaction with this key
    CTransaction tx;
	CWalletTx wtx;

	CEscrow escrow;
    if (!GetTxOfEscrow(*pescrowdb, vchEscrow, 
		escrow, tx))
        throw runtime_error("could not find a escrow with this key");
	uint256 hash, blockHash;
	bool foundEscrowRelease = false;
	
	BOOST_FOREACH(PAIRTYPE(const uint256, CWalletTx)& item, pwalletMain->mapWallet) {
		// get txn hash, read txn index
		vector<vector<unsigned char> > vvch;
		int op, nOut;
		hash = item.second.GetHash();
		if (!GetTransaction(hash, tx, Params().GetConsensus(), blockHash, true))
			continue;
		// skip non-syscoin txns
		if (tx.nVersion != SYSCOIN_TX_VERSION)
			continue;
		if (!DecodeEscrowTx(tx, op, nOut, vvch, -1) 
    		|| !IsEscrowOp(op) 
			|| vvch[0] != vchEscrow
    		|| op != OP_ESCROW_RELEASE)
			continue;
		foundEscrowRelease = true;
		break;
	}
    if (!foundEscrowRelease)
        throw runtime_error("Can only complete an escrow that has been released to you and is not complete already");

      	// check for existing escrow 's
	if (ExistsInMempool(vchEscrow, OP_ESCROW_ACTIVATE) || ExistsInMempool(vchEscrow, OP_ESCROW_RELEASE) || ExistsInMempool(vchEscrow, OP_ESCROW_REFUND) || ExistsInMempool(vchEscrow, OP_ESCROW_COMPLETE)) {
		throw runtime_error("there are pending operations on that escrow");
	}
	std::vector<unsigned char> vchBuyerKeyByte;
    boost::algorithm::unhex(escrow.vchBuyerKey.begin(), escrow.vchBuyerKey.end(), std::back_inserter(vchBuyerKeyByte));
	CPubKey buyerKey(vchBuyerKeyByte);
	CSyscoinAddress buyerAddress(buyerKey.GetID());
	if(!buyerAddress.IsValid())
		throw runtime_error("Buyer address is invalid!");

	UniValue acceptParams(UniValue::VARR);
	acceptParams.push_back(stringFromVch(escrow.vchOffer));
	acceptParams.push_back(static_cast<ostringstream*>( &(ostringstream() << escrow.nQty) )->str());
	acceptParams.push_back(stringFromVch(escrow.vchBuyerKey));
	acceptParams.push_back(stringFromVch(escrow.vchPaymentMessage));
	acceptParams.push_back(buyerAddress.ToString());
	acceptParams.push_back("");
	acceptParams.push_back(tx.GetHash().GetHex());

	UniValue res;
	try
	{
		res = tableRPC.execute("offeraccept", acceptParams);
	}
	catch (UniValue& objError)
	{
		throw runtime_error(find_value(objError, "message").get_str());
	}	
	if (!res.isArray())
		throw runtime_error("Could not complete escrow transaction: Invalid response from offeraccept!");

	const UniValue &arr = res.get_array();
	uint256 acceptTxHash(uint256S(arr[0].get_str()));
	const string &acceptGUID = arr[1].get_str();
	const CWalletTx *wtxAcceptIn;
	wtxAcceptIn = pwalletMain->GetWalletTx(acceptTxHash);
	if (wtxAcceptIn == NULL)
		throw runtime_error("offer accept is not in your wallet");

	escrow.ClearEscrow();
	escrow.vchOfferAcceptLink = vchFromString(acceptGUID);
	escrow.nHeight = chainActive.Tip()->nHeight;
  	CPubKey newDefaultKey;
	pwalletMain->GetKeyFromPool(newDefaultKey); 
	std::vector<unsigned char> vchPubKey(newDefaultKey.begin(), newDefaultKey.end());

    CScript scriptPubKey,scriptPubKeyOrig;
	scriptPubKeyOrig= GetScriptForDestination(newDefaultKey.GetID());
    scriptPubKey << CScript::EncodeOP_N(OP_ESCROW_COMPLETE) << vchEscrow << escrow.vchOffer << OP_2DROP << OP_DROP;
    scriptPubKey += scriptPubKeyOrig;


	vector<CRecipient> vecSend;
	CRecipient recipient;
	CreateRecipient(scriptPubKey, recipient);
	vecSend.push_back(recipient);

	const vector<unsigned char> &data = escrow.Serialize();
	CScript scriptData;
	scriptData << OP_RETURN << data;
	CRecipient fee;
	CreateFeeRecipient(scriptData, data, fee);
	vecSend.push_back(fee);

	SendMoneySyscoin(vecSend, recipient.nAmount+fee.nAmount, false, wtx);
	UniValue ret(UniValue::VARR);
	ret.push_back(wtx.GetHash().GetHex());
	return ret;
}
UniValue escrowrefund(const UniValue& params, bool fHelp) {
    if (fHelp || params.size() != 1)
        throw runtime_error(
		"escrowrefund <escrow guid>\n"
                         "Refunds escrow funds back to buyer, buyer needs to sign the output transaction and send to the network.\n"
                        + HelpRequiringPassphrase());
	if(!HasReachedMainNetForkB2())
		throw runtime_error("Please wait until B2 hardfork starts in before executing this command.");
    // gather & validate inputs
    vector<unsigned char> vchEscrow = vchFromValue(params[0]);

    // this is a syscoin transaction
    CWalletTx wtx;

	EnsureWalletIsUnlocked();

    // look for a transaction with this key
    CTransaction tx;
	CEscrow escrow;
    if (!GetTxOfEscrow(*pescrowdb, vchEscrow, 
		escrow, tx))
        throw runtime_error("could not find a escrow with this key");
    vector<vector<unsigned char> > vvch;
    int op, nOut;
    if (!DecodeEscrowTx(tx, op, nOut, vvch, -1) 
    	|| !IsEscrowOp(op) 
    	|| (op != OP_ESCROW_ACTIVATE))
        throw runtime_error("Refund can only happen on an activated escrow address");

    // unserialize escrow UniValue from txn
    CEscrow theEscrow;
    if(!theEscrow.UnserializeFromTx(tx))
        throw runtime_error("cannot unserialize escrow from txn");
    CTransaction fundingTx;
	uint256 blockHash;
	if (!GetTransaction(escrow.escrowInputTxHash, fundingTx, Params().GetConsensus(), blockHash, true))
		throw runtime_error("failed to escrow transaction");

	std::vector<unsigned char> vchArbiterKeyByte;
    boost::algorithm::unhex(escrow.vchArbiterKey.begin(), escrow.vchArbiterKey.end(), std::back_inserter(vchArbiterKeyByte));
	CPubKey arbiterKey(vchArbiterKeyByte);
	CSyscoinAddress arbiterAddress(arbiterKey.GetID());
	if(!arbiterAddress.IsValid())
		throw runtime_error("Arbiter address is invalid!");

	std::vector<unsigned char> vchBuyerKeyByte;
    boost::algorithm::unhex(escrow.vchBuyerKey.begin(), escrow.vchBuyerKey.end(), std::back_inserter(vchBuyerKeyByte));
	CPubKey buyerKey(vchBuyerKeyByte);
	CSyscoinAddress buyerAddress(buyerKey.GetID());
	if(!buyerAddress.IsValid())
		throw runtime_error("Buyer address is invalid!");

	
	std::vector<unsigned char> vchSellerKeyByte;
    boost::algorithm::unhex(escrow.vchSellerKey.begin(), escrow.vchSellerKey.end(), std::back_inserter(vchSellerKeyByte));
	CPubKey sellerKey(vchSellerKeyByte);
	CSyscoinAddress sellerAddress(sellerKey.GetID());
	if(!sellerAddress.IsValid())
		throw runtime_error("Seller address is invalid!");
	int nOutMultiSig = 0;
	int64_t nExpectedAmount = escrow.nPricePerUnit*escrow.nQty;
	int64_t nEscrowFee = GetEscrowArbiterFee(nExpectedAmount);
	int64_t nExpectedAmountWithEscrowFee = nExpectedAmount+nEscrowFee;
	for(unsigned int i=0;i<fundingTx.vout.size();i++)
	{
		if(fundingTx.vout[i].nValue == nExpectedAmountWithEscrowFee)
		{
			nOutMultiSig = i;
			break;
		}
	} 
	int64_t nAmount = fundingTx.vout[nOutMultiSig].nValue;
	string strEscrowScriptPubKey = HexStr(fundingTx.vout[nOutMultiSig].scriptPubKey.begin(), fundingTx.vout[nOutMultiSig].scriptPubKey.end());
	if(nAmount != nExpectedAmountWithEscrowFee)
		throw runtime_error("Expected amount of escrow does not match what is held in escrow!");
	string strPrivateKey ;
	bool arbiterSigning = false;
	// who is initiating release arbiter or seller?
	try
	{
		arbiterSigning = true;
		// try arbiter
		CKeyID keyID;
		if (!arbiterAddress.GetKeyID(keyID))
			throw runtime_error("Arbiter address does not refer to a key");
		CKey vchSecret;
		if (!pwalletMain->GetKey(keyID, vchSecret))
			throw runtime_error("Private key for arbiter address " + arbiterAddress.ToString() + " is not known");
		strPrivateKey = CSyscoinSecret(vchSecret).ToString();
	}
	catch(...)
	{
		arbiterSigning = false;
		// otherwise try seller
		CKeyID keyID;
		if (!sellerAddress.GetKeyID(keyID))
			throw runtime_error("Seller or Arbiter address does not refer to a key");
		CKey vchSecret;
		if (!pwalletMain->GetKey(keyID, vchSecret))
			throw runtime_error("Seller or Arbiter private keys not known");
		strPrivateKey = CSyscoinSecret(vchSecret).ToString();
	}
     	// check for existing escrow 's
	if (ExistsInMempool(vchEscrow, OP_ESCROW_ACTIVATE) || ExistsInMempool(vchEscrow, OP_ESCROW_RELEASE) || ExistsInMempool(vchEscrow, OP_ESCROW_REFUND) || ExistsInMempool(vchEscrow, OP_ESCROW_COMPLETE)) {
		throw runtime_error("there are pending operations on that escrow");
	}
	// refunds buyer from escrow
	UniValue arrayCreateParams(UniValue::VARR);
	UniValue createTxInputsArray(UniValue::VARR);
	UniValue createTxInputUniValue(UniValue::VOBJ);
	UniValue createAddressUniValue(UniValue::VOBJ);
	createTxInputUniValue.push_back(Pair("txid", escrow.escrowInputTxHash.ToString()));
	createTxInputUniValue.push_back(Pair("vout", nOutMultiSig));
	createTxInputsArray.push_back(createTxInputUniValue);
	if(arbiterSigning)
	{
		createAddressUniValue.push_back(Pair(buyerAddress.ToString(), ValueFromAmount(nExpectedAmount)));
		createAddressUniValue.push_back(Pair(arbiterAddress.ToString(), ValueFromAmount(nEscrowFee)));
	}
	else
	{
		createAddressUniValue.push_back(Pair(buyerAddress.ToString(), ValueFromAmount(nExpectedAmountWithEscrowFee)));
	}	
	arrayCreateParams.push_back(createTxInputsArray);
	arrayCreateParams.push_back(createAddressUniValue);
	UniValue resCreate;
	try
	{
		resCreate = tableRPC.execute("createrawtransaction", arrayCreateParams);
	}
	catch (UniValue& objError)
	{
		throw runtime_error(find_value(objError, "message").get_str());
	}
	if (!resCreate.isStr())
		throw runtime_error("Could not create escrow transaction: Invalid response from createrawtransaction!");
	string createEscrowSpendingTx = resCreate.get_str();

	// Buyer/Arbiter signs it
	UniValue arraySignParams(UniValue::VARR);
	UniValue arraySignInputs(UniValue::VARR);
	UniValue arrayPrivateKeys(UniValue::VARR);

	UniValue signUniValue(UniValue::VOBJ);
	signUniValue.push_back(Pair("txid", escrow.escrowInputTxHash.ToString()));
	signUniValue.push_back(Pair("vout", nOutMultiSig));
	signUniValue.push_back(Pair("scriptPubKey", strEscrowScriptPubKey));
	signUniValue.push_back(Pair("redeemScript", stringFromVch(escrow.vchRedeemScript)));
	arraySignParams.push_back(createEscrowSpendingTx);
	arraySignInputs.push_back(signUniValue);
	arraySignParams.push_back(arraySignInputs);
	arrayPrivateKeys.push_back(strPrivateKey);
	arraySignParams.push_back(arrayPrivateKeys);
	UniValue res;
	try
	{
		res = tableRPC.execute("signrawtransaction", arraySignParams);
	}
	catch (UniValue& objError)
	{
		throw runtime_error(find_value(objError, "message").get_str());
	}
	
	if (!res.isObject())
		throw runtime_error("Could not sign escrow transaction: Invalid response from signrawtransaction!");
	
	const UniValue& o = res.get_obj();
	string hex_str = "";

	const UniValue& hex_value = find_value(o, "hex");
	if (hex_value.isStr())
		hex_str = hex_value.get_str();
	const UniValue& complete_value = find_value(o, "complete");
	bool bComplete = false;
	if (complete_value.isBool())
		bComplete = complete_value.get_bool();

	if(bComplete)
		throw runtime_error("This is not a multisignature escrow!");


	escrow.ClearEscrow();
	escrow.rawTx = vchFromString(hex_str);
	escrow.nHeight = chainActive.Tip()->nHeight;


    CScript scriptPubKey, scriptPubKeyBuyer;
	scriptPubKeyBuyer= GetScriptForDestination(buyerKey.GetID());
    scriptPubKey << CScript::EncodeOP_N(OP_ESCROW_REFUND) << vchEscrow << escrow.vchOffer << OP_2DROP << OP_DROP;
    scriptPubKey += scriptPubKeyBuyer;
	vector<CRecipient> vecSend;
	CRecipient recipient;
	CreateRecipient(scriptPubKey, recipient);
	vecSend.push_back(recipient);

	const vector<unsigned char> &data = escrow.Serialize();
	CScript scriptData;
	scriptData << OP_RETURN << data;
	CRecipient fee;
	CreateFeeRecipient(scriptData, data, fee);
	vecSend.push_back(fee);

	SendMoneySyscoin(vecSend, recipient.nAmount+fee.nAmount, false, wtx);
	UniValue ret(UniValue::VARR);
	ret.push_back(wtx.GetHash().GetHex());
	return ret;
}
UniValue escrowclaimrefund(const UniValue& params, bool fHelp) {
    if (fHelp || params.size() != 1)
        throw runtime_error(
		"escrowclaimrefund <escrow guid>\n"
                        "Claim escrow funds released from seller or arbiter using escrowrefund.\n"
                        + HelpRequiringPassphrase());
	if(!HasReachedMainNetForkB2())
		throw runtime_error("Please wait until B2 hardfork starts in before executing this command.");
    // gather & validate inputs
    vector<unsigned char> vchEscrow = vchFromValue(params[0]);


	EnsureWalletIsUnlocked();

    // look for a transaction with this key
    CTransaction tx;
	CEscrow escrow;
    if (!GetTxOfEscrow(*pescrowdb, vchEscrow, 
		escrow, tx))
        throw runtime_error("could not find a escrow with this key");
	CTransaction fundingTx;
	uint256 blockHash;
	if (!GetTransaction(escrow.escrowInputTxHash, fundingTx, Params().GetConsensus(), blockHash, true))
		throw runtime_error("failed to read escrow transaction");

 	int nOutMultiSig = 0;
	int64_t nExpectedAmount = escrow.nPricePerUnit*escrow.nQty;
	// 0.5% escrow fee
	int64_t nEscrowFee = GetEscrowArbiterFee(nExpectedAmount);
	int64_t nExpectedAmountWithEscrowFee = nExpectedAmount+nEscrowFee;
	for(unsigned int i=0;i<fundingTx.vout.size();i++)
	{
		if(fundingTx.vout[i].nValue == nExpectedAmountWithEscrowFee)
		{
			nOutMultiSig = i;
			break;
		}
	} 
	int64_t nAmount = fundingTx.vout[nOutMultiSig].nValue;
	string strEscrowScriptPubKey = HexStr(fundingTx.vout[nOutMultiSig].scriptPubKey.begin(), fundingTx.vout[nOutMultiSig].scriptPubKey.end());
	if(nAmount != nExpectedAmountWithEscrowFee)
		throw runtime_error("Expected amount of escrow does not match what is held in escrow!");
	// decode rawTx and check it pays enough and it pays to buyer appropriately
	// check that right amount is going to be sent to buyer
	bool foundBuyerPayment = false;
	UniValue arrayDecodeParams(UniValue::VARR);

	arrayDecodeParams.push_back(stringFromVch(escrow.rawTx));
	UniValue decodeRes;
	try
	{
		decodeRes = tableRPC.execute("decoderawtransaction", arrayDecodeParams);
	}
	catch (UniValue& objError)
	{
		throw runtime_error(find_value(objError, "message").get_str());
	}
	if (!decodeRes.isObject())
		throw runtime_error("Could not decode escrow transaction: Invalid response from decoderawtransaction!");
	const UniValue& decodeo = decodeRes.get_obj();
	const UniValue& vout_value = find_value(decodeo, "vout");
	if (!vout_value.isArray())
		throw runtime_error("Could not decode escrow transaction: Can't find vout's from transaction!");	
	UniValue vouts = vout_value.get_array();
    for (unsigned int idx = 0; idx < vouts.size(); idx++) {
        const UniValue& vout = vouts[idx];					
		const UniValue &voutObj = vout.get_obj();					
		const UniValue &voutValue = find_value(voutObj, "value");
		if(!voutValue.isNum())
			throw runtime_error("Could not decode escrow transaction: Invalid vout value!");
		int64_t iVout = AmountFromValue(voutValue);
		UniValue scriptPubKeyValue = find_value(voutObj, "scriptPubKey");
		if(!scriptPubKeyValue.isObject())
			throw runtime_error("Could not decode escrow transaction: Invalid scriptPubKey UniValue!");
		const UniValue &scriptPubKeyValueObj = scriptPubKeyValue.get_obj();	
		const UniValue &addressesValue = find_value(scriptPubKeyValueObj, "addresses");
		if(!addressesValue.isArray())
			throw runtime_error("Could not decode escrow transaction: Invalid addresses UniValue!");

		UniValue addresses = addressesValue.get_array();
		for (unsigned int idx = 0; idx < addresses.size(); idx++) {
			const UniValue& address = addresses[idx];
			if(!address.isStr())
				throw runtime_error("Could not decode escrow transaction: Invalid address UniValue!");
			string strAddress = address.get_str();
			CSyscoinAddress payoutAddress(strAddress);
			if(IsMine(*pwalletMain, payoutAddress.Get()))
			{
				if(!foundBuyerPayment)
				{
					if(iVout == nExpectedAmountWithEscrowFee || iVout == nExpectedAmount)
					{
						foundBuyerPayment = true;
						break;
					}
				}
			}
		}
	}

	// get buyer's private key for signing
	CKeyID keyID;
	std::vector<unsigned char> vchBuyerKeyByte;
    boost::algorithm::unhex(escrow.vchBuyerKey.begin(), escrow.vchBuyerKey.end(), std::back_inserter(vchBuyerKeyByte));
	CPubKey buyerKey(vchBuyerKeyByte);
	CSyscoinAddress buyerAddress(buyerKey.GetID());
	if(!buyerAddress.IsValid())
		throw runtime_error("Buyer address is invalid!");

	if (!buyerAddress.GetKeyID(keyID))
		throw runtime_error("Buyer address does not refer to a key");
	CKey vchSecret;
	if (!pwalletMain->GetKey(keyID, vchSecret))
		throw runtime_error("Private key for buyer address " + buyerAddress.ToString() + " is not known");
	string strPrivateKey = CSyscoinSecret(vchSecret).ToString();
	if(!foundBuyerPayment)
		throw runtime_error("Expected payment amount from escrow does not match what was expected by the buyer!");
      	// check for existing escrow 's
	if (ExistsInMempool(vchEscrow, OP_ESCROW_ACTIVATE) || ExistsInMempool(vchEscrow, OP_ESCROW_RELEASE) || ExistsInMempool(vchEscrow, OP_ESCROW_REFUND) || ExistsInMempool(vchEscrow, OP_ESCROW_COMPLETE) ) {
		throw runtime_error("there are pending operations on that escrow");
	}
    // Seller signs it
	UniValue arraySignParams(UniValue::VARR);
	UniValue arraySignInputs(UniValue::VARR);
	UniValue arrayPrivateKeys(UniValue::VARR);
	UniValue signUniValue(UniValue::VOBJ);
	signUniValue.push_back(Pair("txid", escrow.escrowInputTxHash.ToString()));
	signUniValue.push_back(Pair("vout", nOutMultiSig));
	signUniValue.push_back(Pair("scriptPubKey", strEscrowScriptPubKey));
	signUniValue.push_back(Pair("redeemScript", stringFromVch(escrow.vchRedeemScript)));
	arraySignParams.push_back(stringFromVch(escrow.rawTx));
	arraySignInputs.push_back(signUniValue);
	arraySignParams.push_back(arraySignInputs);
	arrayPrivateKeys.push_back(strPrivateKey);
	arraySignParams.push_back(arrayPrivateKeys);
	UniValue res;
	try
	{
		res = tableRPC.execute("signrawtransaction", arraySignParams);
	}
	catch (UniValue& objError)
	{
		throw runtime_error(find_value(objError, "message").get_str());
	}
	if (!res.isObject())
		throw runtime_error("Could not sign escrow transaction: Invalid response from signrawtransaction!");
	
	const UniValue& o = res.get_obj();
	string hex_str = "";

	const UniValue& hex_value = find_value(o, "hex");
	if (hex_value.isStr())
		hex_str = hex_value.get_str();
	const UniValue& complete_value = find_value(o, "complete");
	bool bComplete = false;
	if (complete_value.isBool())
		bComplete = complete_value.get_bool();

	if(!bComplete)
		throw runtime_error("Could not sign escrow transaction. It is showing as incomplete, you may not allowed to complete this request at this time.");

	// broadcast the payment transaction
	UniValue arraySendParams(UniValue::VARR);
	arraySendParams.push_back(hex_str);
	UniValue returnRes;
	try
	{
		returnRes = tableRPC.execute("signrawtransaction", arraySignParams);
	}
	catch (UniValue& objError)
	{
		throw runtime_error(find_value(objError, "message").get_str());
	}
	return returnRes;
}

UniValue escrowinfo(const UniValue& params, bool fHelp) {
    if (fHelp || 1 != params.size())
        throw runtime_error("escrowinfo <guid>\n"
                "Show stored values of a single escrow and its .\n");

    vector<unsigned char> vchEscrow = vchFromValue(params[0]);

    // look for a transaction with this key, also returns
    // an escrow UniValue if it is found
    CTransaction tx;

	vector<CEscrow> vtxPos;

    UniValue oEscrow(UniValue::VOBJ);
    vector<unsigned char> vchValue;

	if (!pescrowdb->ReadEscrow(vchEscrow, vtxPos) || vtxPos.empty())
		  throw runtime_error("failed to read from escrow DB");
	CEscrow ca = vtxPos.back();
	
	
    string sHeight = strprintf("%llu", ca.nHeight);
    oEscrow.push_back(Pair("escrow", stringFromVch(vchEscrow)));
	string sTime;
	CBlockIndex *pindex = chainActive[ca.nHeight];
	if (pindex) {
		sTime = strprintf("%llu", pindex->nTime);
	}
	oEscrow.push_back(Pair("time", sTime));
	oEscrow.push_back(Pair("seller", ca.seller));
	oEscrow.push_back(Pair("arbiter", ca.arbiter));
	oEscrow.push_back(Pair("buyerkey", stringFromVch(ca.vchBuyerKey)));
	oEscrow.push_back(Pair("offer", stringFromVch(ca.vchOffer)));
	oEscrow.push_back(Pair("offeracceptlink", stringFromVch(ca.vchOfferAcceptLink)));
	oEscrow.push_back(Pair("systotal", ValueFromAmount(ca.nPricePerUnit * ca.nQty)));
	int64_t nEscrowFee = GetEscrowArbiterFee(ca.nPricePerUnit * ca.nQty);
	oEscrow.push_back(Pair("sysfee", ValueFromAmount(nEscrowFee)));
	string sTotal = strprintf("%llu SYS", (ca.nPricePerUnit/COIN)*ca.nQty);
	oEscrow.push_back(Pair("total", sTotal));
    oEscrow.push_back(Pair("txid", ca.txHash.GetHex()));
    oEscrow.push_back(Pair("height", sHeight));
	string strMessage = string("");
	if(!DecryptMessage(ca.vchSellerKey, ca.vchPaymentMessage, strMessage))
		strMessage = string("Encrypted for owner of offer");
	oEscrow.push_back(Pair("pay_message", strMessage));

	
    return oEscrow;
}

UniValue escrowlist(const UniValue& params, bool fHelp) {
    if (fHelp || 1 < params.size())
        throw runtime_error("escrowlist [<escrow>]\n"
                "list my own escrows");
	vector<unsigned char> vchName;

	if (params.size() == 1)
		vchName = vchFromValue(params[0]);
    vector<unsigned char> vchNameUniq;
    if (params.size() == 1)
        vchNameUniq = vchFromValue(params[0]);

    UniValue oRes(UniValue::VARR);
    map< vector<unsigned char>, int > vNamesI;
    map< vector<unsigned char>, UniValue > vNamesO;

    uint256 blockHash;
    uint256 hash;
    CTransaction tx, dbtx;

    vector<unsigned char> vchValue;
    uint64_t nHeight;

    BOOST_FOREACH(PAIRTYPE(const uint256, CWalletTx)& item, pwalletMain->mapWallet)
    {
		int expired = 0;
        // get txn hash, read txn index
        hash = item.second.GetHash();
		const CWalletTx &wtx = item.second;        // skip non-syscoin txns
        if (wtx.nVersion != SYSCOIN_TX_VERSION)
            continue;
		// decode txn, skip non-alias txns
		vector<vector<unsigned char> > vvch;
		int op, nOut;
		if (!DecodeEscrowTx(wtx, op, nOut, vvch, -1) || !IsEscrowOp(op))
			continue;
		vchName = vvch[0];
		vector<CEscrow> vtxPos;
		if (!pescrowdb->ReadEscrow(vchName, vtxPos) || vtxPos.empty())
			continue;
		CEscrow escrow = vtxPos.back();
		// skip this escrow if it doesn't match the given filter value
		if (vchNameUniq.size() > 0 && vchNameUniq != vchName)
			continue;
		// get last active name only
		if (vNamesI.find(vchName) != vNamesI.end() && (escrow.nHeight < vNamesI[vchName] || vNamesI[vchName] < 0))
			continue;

		if (!GetTransaction(escrow.txHash, tx, Params().GetConsensus(), blockHash, true))
			continue;
		nHeight = escrow.nHeight;
        // build the output UniValue
        UniValue oName(UniValue::VOBJ);
        oName.push_back(Pair("escrow", stringFromVch(vchName)));
		string sTime;
		CBlockIndex *pindex = chainActive[escrow.nHeight];
		if (pindex) {
			sTime = strprintf("%llu", pindex->nTime);
		}
		oName.push_back(Pair("time", sTime));
		oName.push_back(Pair("seller", escrow.seller));
		oName.push_back(Pair("arbiter", escrow.arbiter));
		oName.push_back(Pair("buyerkey", stringFromVch(escrow.vchBuyerKey)));
		oName.push_back(Pair("offer", stringFromVch(escrow.vchOffer)));
		oName.push_back(Pair("offeraccept", stringFromVch(escrow.vchOfferAcceptLink)));

		string sTotal = strprintf("%llu SYS", (escrow.nPricePerUnit/COIN)*escrow.nQty);
		oName.push_back(Pair("total", sTotal));
		if(nHeight + GetEscrowExpirationDepth() - chainActive.Tip()->nHeight <= 0)
		{
			expired = 1;
		}  
		string status = "unknown";
		if(op == OP_ESCROW_ACTIVATE)
			status = "inescrow";
		else if(op == OP_ESCROW_RELEASE)
			status = "escrowreleased";
		else if(op == OP_ESCROW_REFUND)
			status = "escrowrefunded";
		else if(op == OP_ESCROW_COMPLETE)
			status = "complete";
		oName.push_back(Pair("status", status));

		oName.push_back(Pair("expired", expired));
 
		vNamesI[vchName] = nHeight;
		vNamesO[vchName] = oName;	
    
	}
    BOOST_FOREACH(const PAIRTYPE(vector<unsigned char>, UniValue)& item, vNamesO)
        oRes.push_back(item.second);
    return oRes;
}


UniValue escrowhistory(const UniValue& params, bool fHelp) {
    if (fHelp || 1 != params.size())
        throw runtime_error("escrowhistory <escrow>\n"
                "List all stored values of an escrow.\n");

    UniValue oRes(UniValue::VARR);
    vector<unsigned char> vchEscrow = vchFromValue(params[0]);
    string escrow = stringFromVch(vchEscrow);

    {
        vector<CEscrow> vtxPos;
        if (!pescrowdb->ReadEscrow(vchEscrow, vtxPos) || vtxPos.empty())
            throw runtime_error("failed to read from escrow DB");

        CEscrow txPos2;
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
            UniValue oEscrow(UniValue::VOBJ);
            uint64_t nHeight;
			nHeight = txPos2.nHeight;
			oEscrow.push_back(Pair("escrow", escrow));
			string sTime;
			CBlockIndex *pindex = chainActive[txPos2.nHeight];
			if (pindex) {
				sTime = strprintf("%llu", pindex->nTime);
			}
			oEscrow.push_back(Pair("time", sTime));
			oEscrow.push_back(Pair("txid", tx.GetHash().GetHex()));
			oEscrow.push_back(Pair("seller", txPos2.seller));
			oEscrow.push_back(Pair("arbiter", txPos2.arbiter));
			oEscrow.push_back(Pair("buyerkey", stringFromVch(txPos2.vchBuyerKey)));
			oEscrow.push_back(Pair("offer", stringFromVch(txPos2.vchOffer)));
			oEscrow.push_back(Pair("offeracceptlink", stringFromVch(txPos2.vchOfferAcceptLink)));

			string sTotal = strprintf("%llu SYS", (txPos2.nPricePerUnit/COIN)*txPos2.nQty);
			oEscrow.push_back(Pair("total", sTotal));
			if(nHeight + GetEscrowExpirationDepth() - chainActive.Tip()->nHeight <= 0)
			{
				expired = 1;
			}  

			oEscrow.push_back(Pair("expired", expired));
			oRes.push_back(oEscrow);
        }
        
    }
    return oRes;
}

UniValue escrowfilter(const UniValue& params, bool fHelp) {
    if (fHelp || params.size() > 5)
        throw runtime_error(
                "escrowfilter [[[[[search string] maxage=36000] from=0] nb=0] stat]\n"
                        "scan and filter escrows\n"
                        "[search string] : Find arbiter or seller via alias name or an escrow GUID, empty means all escrows\n"
                        "[maxage] : look in last [maxage] blocks\n"
                        "[from] : show results from number [from]\n"
                        "[nb] : show [nb] results, 0 means all\n"
                        "[stats] : show some stats instead of results\n"
                        "escrowfilter \"\" 5 # list Escrows updated in last 5 blocks\n");

    string strSearch;
    int nFrom = 0;
    int nNb = 0;
    int nMaxAge = GetEscrowExpirationDepth();
    bool fStat = false;
    int nCountFrom = 0;
    int nCountNb = 0;

    if (params.size() > 0)
        strSearch = params[0].get_str();

    if (params.size() > 1)
        nMaxAge = params[1].get_int();

    if (params.size() > 2)
        nFrom = params[2].get_int();

    if (params.size() > 3)
        nNb = params[3].get_int();

    if (params.size() > 4)
        fStat = (params[4].get_str() == "stat" ? true : false);

    //CEscrowDB dbEscrow("r");
    UniValue oRes(UniValue::VARR);

    vector<unsigned char> vchEscrow;
    vector<pair<vector<unsigned char>, CEscrow> > escrowScan;
    if (!pescrowdb->ScanEscrows(vchEscrow, 100000000, escrowScan))
        throw runtime_error("scan failed");

    pair<vector<unsigned char>, CEscrow> pairScan;
    BOOST_FOREACH(pairScan, escrowScan) {
		CEscrow txEscrow = pairScan.second;
		string escrow = stringFromVch(pairScan.first);
		string offer = stringFromVch(txEscrow.vchOffer);
        if (strSearch != "" && strSearch != escrow && strSearch != txEscrow.arbiter && strSearch != txEscrow.seller)
            continue;

        
        int nHeight = txEscrow.nHeight;

        // max age
        if (nMaxAge != 0 && chainActive.Tip()->nHeight - nHeight >= nMaxAge)
            continue;
        // from limits
        nCountFrom++;
        if (nCountFrom < nFrom + 1)
            continue;
        CTransaction tx;
        uint256 blockHash;
		if (!GetTransaction(txEscrow.txHash, tx, Params().GetConsensus(), blockHash, true))
			continue;

		int expired = 0;

        UniValue oEscrow(UniValue::VOBJ);
        oEscrow.push_back(Pair("escrow", escrow));
		if(nHeight + GetEscrowExpirationDepth() - chainActive.Tip()->nHeight <= 0)
		{
			expired = 1;
		} 
		string sTime;
		CBlockIndex *pindex = chainActive[txEscrow.nHeight];
		if (pindex) {
			sTime = strprintf("%llu", pindex->nTime);
		}
		oEscrow.push_back(Pair("time", sTime));
		oEscrow.push_back(Pair("expired", expired));
		oEscrow.push_back(Pair("seller", txEscrow.seller));
		oEscrow.push_back(Pair("arbiter", txEscrow.arbiter));
		oEscrow.push_back(Pair("buyerkey", stringFromVch(txEscrow.vchBuyerKey)));
		oEscrow.push_back(Pair("offer", stringFromVch(txEscrow.vchOffer)));
		oEscrow.push_back(Pair("offeracceptlink", stringFromVch(txEscrow.vchOfferAcceptLink)));

		string sTotal = strprintf("%llu SYS", (txEscrow.nPricePerUnit/COIN)*txEscrow.nQty);
		oEscrow.push_back(Pair("total", sTotal));
        oRes.push_back(oEscrow);

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

UniValue escrowscan(const UniValue& params, bool fHelp) {
    if (fHelp || 2 > params.size())
        throw runtime_error(
                "escrowscan [<start-escrow>] [<max-returned>]\n"
                        "scan all escrows, starting at start-escrow and returning a maximum number of entries (default 500)\n");

    vector<unsigned char> vchEscrow;
    int nMax = 500;
    if (params.size() > 0) {
        vchEscrow = vchFromValue(params[0]);
    }

    if (params.size() > 1) {
        nMax = params[1].get_int();
    }

    //CEscrowDB dbEscrow("r");
    UniValue oRes(UniValue::VARR);

    vector<pair<vector<unsigned char>, CEscrow> > escrowScan;
    if (!pescrowdb->ScanEscrows(vchEscrow, nMax, escrowScan))
        throw runtime_error("scan failed");

    pair<vector<unsigned char>, CEscrow> pairScan;
    BOOST_FOREACH(pairScan, escrowScan) {
        UniValue oEscrow(UniValue::VOBJ);
        string escrow = stringFromVch(pairScan.first);
        oEscrow.push_back(Pair("escrow", escrow));
        CTransaction tx;
        CEscrow txEscrow = pairScan.second;
        uint256 blockHash;
		int expired = 0;
        int nHeight = txEscrow.nHeight;
        
		if (!GetTransaction(txEscrow.txHash, tx, Params().GetConsensus(), blockHash, true))
			continue;


		if(nHeight + GetEscrowExpirationDepth() - chainActive.Tip()->nHeight <= 0)
		{
			expired = 1;
		}  
		string sTime;
		CBlockIndex *pindex = chainActive[txEscrow.nHeight];
		if (pindex) {
			sTime = strprintf("%llu", pindex->nTime);
		}
		oEscrow.push_back(Pair("time", sTime));
		oEscrow.push_back(Pair("seller", txEscrow.seller));
		oEscrow.push_back(Pair("arbiter", txEscrow.arbiter));
		oEscrow.push_back(Pair("buyerkey", stringFromVch(txEscrow.vchBuyerKey)));
		oEscrow.push_back(Pair("offer", stringFromVch(txEscrow.vchOffer)));
		oEscrow.push_back(Pair("offeracceptlink", stringFromVch(txEscrow.vchOfferAcceptLink)));
		string sTotal = strprintf("%ll SYS", (txEscrow.nPricePerUnit/COIN)*txEscrow.nQty);
		oEscrow.push_back(Pair("total", sTotal));
		oEscrow.push_back(Pair("expired", expired));
			
		oRes.push_back(oEscrow);
    }

    return oRes;
}



