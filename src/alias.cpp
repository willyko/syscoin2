// Copyright (c) 2014 Syscoin Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file license.txt or http://www.opensource.org/licenses/mit-license.php.
//
#include "alias.h"
#include "offer.h"
#include "escrow.h"
#include "message.h"
#include "cert.h"
#include "init.h"
#include "main.h"
#include "util.h"
#include "random.h"
#include "wallet/wallet.h"
#include "rpcserver.h"
#include "base58.h"
#include "txmempool.h"
#include "txdb.h"
#include "chainparams.h"
#include "policy/policy.h"

#include <boost/algorithm/string.hpp>
#include <boost/xpressive/xpressive_dynamic.hpp>
#include <boost/foreach.hpp>
#include <boost/thread.hpp>
using namespace std;
CAliasDB *paliasdb = NULL;
COfferDB *pofferdb = NULL;
CCertDB *pcertdb = NULL;
CEscrowDB *pescrowdb = NULL;
CMessageDB *pmessagedb = NULL;
extern void SendMoneySyscoin(const vector<CRecipient> &vecSend, CAmount nValue, bool fSubtractFeeFromAmount, CWalletTx& wtxNew, const CWalletTx* wtxIn=NULL, bool syscoinTx=true);
unsigned int QtyOfPendingAcceptsInMempool(const vector<unsigned char>& vchToFind)
{
	LOCK(mempool.cs);
	unsigned int nQty = 0;
	for (CTxMemPool::indexed_transaction_set::iterator mi = mempool.mapTx.begin();
             mi != mempool.mapTx.end(); ++mi)
        {
        const CTransaction& tx = mi->GetTx();
		if (tx.IsCoinBase() || !CheckFinalTx(tx))
			continue;
		vector<vector<unsigned char> > vvch;
		int op, nOut;
		
		if(DecodeOfferTx(tx, op, nOut, vvch, -1)) {
			if(op == OP_OFFER_ACCEPT)
			{
				if(vvch.size() >= 1 && vvch[0] == vchToFind)
				{
					COffer theOffer(tx);
					COfferAccept theOfferAccept;
					if (theOffer.IsNull())
						continue;
					if(theOffer.GetAcceptByHash(vvch[1], theOfferAccept))
					{
						nQty += theOfferAccept.nQty;
					}
				}
			}
		}		
	}
	return nQty;

}
bool ExistsInMempool(const std::vector<unsigned char> &vchToFind, opcodetype type)
{
	LOCK(mempool.cs);
	for (CTxMemPool::indexed_transaction_set::iterator mi = mempool.mapTx.begin();
             mi != mempool.mapTx.end(); ++mi)
        {
        const CTransaction& tx = mi->GetTx();
		if (tx.IsCoinBase() || !CheckFinalTx(tx))
			continue;
		vector<vector<unsigned char> > vvch;
		int op, nOut;
		if(DecodeAliasTx(tx, op, nOut, vvch, -1))
		{
		}
		else if(DecodeOfferTx(tx, op, nOut, vvch, -1))		
		{
		}
		else if(DecodeCertTx(tx, op, nOut, vvch, -1))
		{
		}
		else if(DecodeEscrowTx(tx, op, nOut, vvch, -1))
		{
		}
		else if(DecodeMessageTx(tx, op, nOut, vvch, -1))
		{
		}
		else
			continue;
		if(op == type)
		{
			if(vvch.size() >= 1 && vchToFind == vvch[0])
			{
				return true;
			}
		}
	}
	return false;

}
bool HasReachedMainNetForkB2()
{
	bool fTestNet = false;
    if (Params().NetworkIDString() != "main")
		fTestNet = true;
	return fTestNet || (!fTestNet && chainActive.Tip()->nHeight >= 1);
}

CAmount convertCurrencyCodeToSyscoin(const vector<unsigned char> &vchCurrencyCode, const float &nPrice, const unsigned int &nHeight, int &precision)
{
	CAmount sysPrice;
	CAmount nRate;
	vector<string> rateList;
	if(getCurrencyToSYSFromAlias(vchCurrencyCode, nRate, nHeight, rateList, precision) == "")
	{
		float price = nPrice*(float)nRate;
		sysPrice = CAmount(price);
	}
	return sysPrice;
}
// refund an offer accept by creating a transaction to send coins to offer accepter, and an offer accept back to the offer owner. 2 Step process in order to use the coins that were sent during initial accept.
string getCurrencyToSYSFromAlias(const vector<unsigned char> &vchCurrency, CAmount &nFee, const unsigned int &nHeightToFind, vector<string>& rateList, int &precision)
{
	vector<unsigned char> vchName = vchFromString("SYS_RATES");
	string currencyCodeToFind = stringFromVch(vchCurrency);
	// check for alias existence in DB
	vector<CAliasIndex> vtxPos;
	if (!paliasdb->ReadAlias(vchName, vtxPos) || vtxPos.empty())
	{
		if(fDebug)
			LogPrintf("getCurrencyToSYSFromAlias() Could not find SYS_RATES alias\n");
		return "1";
	}
	if (vtxPos.size() < 1)
	{
		if(fDebug)
			LogPrintf("getCurrencyToSYSFromAlias() Could not find SYS_RATES alias (vtxPos.size() == 0)\n");
		return "1";
	}
	CAliasIndex foundAlias;
	for(unsigned int i=0;i<vtxPos.size();i++) {
        CAliasIndex a = vtxPos[i];
        if(a.nHeight <= nHeightToFind) {
            foundAlias = a;
        }
		else
			break;
    }
	if(foundAlias.IsNull())
		foundAlias = vtxPos.back();
	// get transaction pointed to by alias
	uint256 blockHash;
	uint256 txHash = foundAlias.txHash;
	CTransaction tx;
	if (!GetTransaction(txHash, tx, Params().GetConsensus(), blockHash, true))
	{
		if(fDebug)
			LogPrintf("getCurrencyToSYSFromAlias() Failed to read transaction from disk\n");
		return "1";
	}


	bool found = false;
	string value = stringFromVch(foundAlias.vchValue);
	
	UniValue outerValue(UniValue::VSTR);
	bool read = outerValue.read(value);
	if (read)
	{
		UniValue outerObj = outerValue.get_obj();
		UniValue ratesValue = find_value(outerObj, "rates");
		if (ratesValue.isArray())
		{
			UniValue codes = ratesValue.get_array();
			for (unsigned int idx = 0; idx < codes.size(); idx++) {
				const UniValue& code = codes[idx];					
				UniValue codeObj = code.get_obj();					
				UniValue currencyNameValue = find_value(codeObj, "currency");
				UniValue currencyAmountValue = find_value(codeObj, "rate");
				if (currencyNameValue.isStr())
				{		
					string currencyCode = currencyNameValue.get_str();
					rateList.push_back(currencyCode);
					if(currencyCodeToFind == currencyCode)
					{
						UniValue precisionValue = find_value(codeObj, "precision");
						if(precisionValue.isNum())
						{
							precision = precisionValue.get_int();
						}
						if(currencyAmountValue.isNum())
						{
							found = true;
							try{
								nFee = AmountFromValue(currencyAmountValue.get_real());
							}
							catch(std::runtime_error& err)
							{
								nFee = currencyAmountValue.get_int()*COIN;
							}								
						}
					}
				}
			}
		}
		
	}
	else
	{
		if(fDebug)
			printf("getCurrencyToSYSFromAlias() Failed to get value from alias\n");
		return "1";
	}
	if(!found)
	{
		if(fDebug)
			LogPrintf("getCurrencyToSYSFromAlias() currency %s not found in SYS_RATES alias\n", stringFromVch(vchCurrency).c_str());
		return "0";
	}
	return "";

}
void PutToAliasList(std::vector<CAliasIndex> &aliasList, CAliasIndex& index) {
	int i = aliasList.size() - 1;
	BOOST_REVERSE_FOREACH(CAliasIndex &o, aliasList) {
        if(index.nHeight != 0 && o.nHeight == index.nHeight) {
        	aliasList[i] = index;
            return;
        }
        else if(!o.txHash.IsNull() && o.txHash == index.txHash) {
        	aliasList[i] = index;
            return;
        }
        i--;
	}
    aliasList.push_back(index);
}

bool IsAliasOp(int op) {
	return op == OP_ALIAS_ACTIVATE
			|| op == OP_ALIAS_UPDATE;
}
string aliasFromOp(int op) {
	switch (op) {
	case OP_ALIAS_UPDATE:
		return "aliasupdate";
	case OP_ALIAS_ACTIVATE:
		return "aliasactivate";
	default:
		return "<unknown alias op>";
	}
}
int GetSyscoinDataOutput(const CTransaction& tx) {
   txnouttype whichType;
   for(unsigned int i = 0; i<tx.vout.size();i++) {
		if (!IsStandard(tx.vout[i].scriptPubKey, whichType))
			continue;
		if (whichType == TX_NULL_DATA)
		{
			return i;
		}
	}
   return -1;
}

int GetSyscoinTxVersion()
{
	return SYSCOIN_TX_VERSION;
}
/**
 * [IsAliasMine description]
 * @param  tx [description]
 * @return    [description]
 */
bool IsAliasMine(const CTransaction& tx) {
	if (tx.nVersion != SYSCOIN_TX_VERSION)
		return false;

	vector<vector<unsigned char> > vvch;
	int op, nOut;

	if (!DecodeAliasTx(tx, op, nOut, vvch, -1))
		return false;

	if (!IsAliasOp(op))
		return false;

	const CTxOut& txout = tx.vout[nOut];
	if (pwalletMain->IsMine(txout)) {
		return true;
	}

	return false;
}

bool CheckAliasInputs(const CTransaction &tx,
		CValidationState &state, const CCoinsViewCache &inputs, bool fBlock,
		bool fMiner, bool fJustCheck, int nHeight) {

	if (!tx.IsCoinBase()) {
		if (fDebug)
			LogPrintf("*** %d %d %s %s %s %s\n", nHeight,
				chainActive.Tip()->nHeight, tx.GetHash().ToString().c_str(),
				fBlock ? "BLOCK" : "", fMiner ? "MINER" : "",
				fJustCheck ? "JUSTCHECK" : "");

		bool found = false;
		const COutPoint *prevOutput = NULL;
		CCoins prevCoins;
		int prevOp;
		vector<vector<unsigned char> > vvchPrevArgs;
		// Strict check - bug disallowed
		for (unsigned int i = 0; i < tx.vin.size(); i++) {
			prevOutput = &tx.vin[i].prevout;
			inputs.GetCoins(prevOutput->hash, prevCoins);
			if (DecodeAliasScript(prevCoins.vout[prevOutput->n].scriptPubKey,
					prevOp, vvchPrevArgs)) {
				found = true;
				break;
			}
		}
		if(!found)vvchPrevArgs.clear();
		// Make sure alias outputs are not spent by a regular transaction, or the alias would be lost
		if (tx.nVersion != SYSCOIN_TX_VERSION) {
			if (found)
				return error(
						"CheckAliasInputs() : a non-syscoin transaction with a syscoin input");
			LogPrintf("CheckAliasInputs() : non-syscoin transaction\n");
			return true;
		}

		// decode alias info from transaction
		vector<vector<unsigned char> > vvchArgs;
		int op, nOut;
		if (!DecodeAliasTx(tx, op, nOut, vvchArgs, -1))
			return error(
					"CheckAliasInputs() : could not decode syscoin alias info from tx %s",
					tx.GetHash().GetHex().c_str());
		// unserialize alias UniValue from txn, check for valid
		CAliasIndex theAlias(tx);
		if (theAlias.IsNull())
			return error("CheckAliasInputs() : null alias");
		if(theAlias.vchValue.size() > MAX_VALUE_LENGTH)
		{
			return error("alias value too big");
		}
		if(theAlias.vchPubKey.size() > MAX_NAME_LENGTH)
		{
			return error("alias pub key too big");
		}
		if (vvchArgs[0].size() > MAX_NAME_LENGTH)
			return error("alias hex guid too long");
		switch (op) {

		case OP_ALIAS_ACTIVATE:
			break;

		case OP_ALIAS_UPDATE:

			if (!found)
				return error("aliasupdate previous tx not found");
			if (prevOp != OP_ALIAS_ACTIVATE && prevOp != OP_ALIAS_UPDATE)
				return error("aliasupdate tx without correct previous alias tx");

			// Check name
			if (vvchPrevArgs[0] != vvchArgs[0])
				return error("CheckAliasInputs() : aliasupdate alias mismatch");

			break;

		default:
			return error(
					"CheckAliasInputs() : alias transaction has unknown op");
		}

		if (fBlock || (!fBlock && !fMiner && !fJustCheck)) {

			// get the alias from the DB
			vector<CAliasIndex> vtxPos;
			if (paliasdb->ExistsAlias(vvchArgs[0]) && !fJustCheck) {
				if (!paliasdb->ReadAlias(vvchArgs[0], vtxPos))
					return error(
							"CheckAliasInputs() : failed to read from alias DB");
			}

			if (!fMiner && !fJustCheck && chainActive.Tip()->nHeight != nHeight) {
				if(!vtxPos.empty())
				{
					const CAliasIndex& dbAlias = vtxPos.back();
					if(theAlias.vchValue.empty())
						theAlias.vchValue = dbAlias.vchValue;	
					if(theAlias.vchPubKey.empty())
						theAlias.vchPubKey = dbAlias.vchPubKey;
				}
				int nHeight = chainActive.Tip()->nHeight;
	
				theAlias.nHeight = nHeight;
				theAlias.txHash = tx.GetHash();

				PutToAliasList(vtxPos, theAlias);

				{
				TRY_LOCK(cs_main, cs_trymain);

				if (!paliasdb->WriteAlias(vvchArgs[0], vtxPos))
					return error( "CheckAliasInputs() :  failed to write to alias DB");
				
				if(fDebug)
					LogPrintf(
						"CONNECTED ALIAS: name=%s  op=%s  hash=%s  height=%d\n",
						stringFromVch(vvchArgs[0]).c_str(),
						aliasFromOp(op).c_str(),
						tx.GetHash().ToString().c_str(), nHeight);
				}
			}
			
		}
	}
	return true;
}

string stringFromValue(const UniValue& value) {
	string strName = value.get_str();
	return strName;
}

vector<unsigned char> vchFromValue(const UniValue& value) {
	string strName = value.get_str();
	unsigned char *strbeg = (unsigned char*) strName.c_str();
	return vector<unsigned char>(strbeg, strbeg + strName.size());
}

std::vector<unsigned char> vchFromString(const std::string &str) {
	unsigned char *strbeg = (unsigned char*) str.c_str();
	return vector<unsigned char>(strbeg, strbeg + str.size());
}

string stringFromVch(const vector<unsigned char> &vch) {
	string res;
	vector<unsigned char>::const_iterator vi = vch.begin();
	while (vi != vch.end()) {
		res += (char) (*vi);
		vi++;
	}
	return res;
}
bool GetSyscoinData(const CTransaction &tx, vector<unsigned char> &vchData)
{
	int nOut = GetSyscoinDataOutput(tx);
    if(nOut == -1)
	   return false;

	const CScript &scriptPubKey = tx.vout[nOut].scriptPubKey;
	CScript::const_iterator pc = scriptPubKey.begin();
	opcodetype opcode;
	if (!scriptPubKey.GetOp(pc, opcode))
		return false;
	if(opcode != OP_RETURN)
		return false;
	if (!scriptPubKey.GetOp(pc, opcode, vchData))
		return false;
	return true;
}
bool CAliasIndex::UnserializeFromTx(const CTransaction &tx) {
	vector<unsigned char> vchData;
	if(!GetSyscoinData(tx, vchData))
		return false;
    try {
        CDataStream dsAlias(vchData, SER_NETWORK, PROTOCOL_VERSION);
        dsAlias >> *this;
    } catch (std::exception &e) {
        return false;
    }
    return true;
}
const vector<unsigned char> CAliasIndex::Serialize() {
    CDataStream dsAlias(SER_NETWORK, PROTOCOL_VERSION);
    dsAlias << *this;
    const vector<unsigned char> vchData(dsAlias.begin(), dsAlias.end());
    return vchData;

}
bool CAliasDB::ScanNames(const std::vector<unsigned char>& vchName,
		unsigned int nMax,
		vector<pair<vector<unsigned char>, CAliasIndex> >& nameScan) {

	boost::scoped_ptr<CDBIterator> pcursor(NewIterator());
	pcursor->Seek(make_pair(string("namei"), vchName));
	while (pcursor->Valid()) {
		boost::this_thread::interruption_point();
		pair<string, vector<unsigned char> > key;
		try {
			if (pcursor->GetKey(key) && key.first == "namei") {
				vector<unsigned char> vchName = key.second;
				vector<CAliasIndex> vtxPos;
				pcursor->GetValue(vtxPos);
				CAliasIndex txPos;
				if (!vtxPos.empty())
					txPos = vtxPos.back();
				nameScan.push_back(make_pair(vchName, txPos));
			}
			if (nameScan.size() >= nMax)
				break;

			pcursor->Next();
		} catch (std::exception &e) {
			return error("%s() : deserialize error", __PRETTY_FUNCTION__);
		}
	}
	return true;
}

void rescanforaliases(CBlockIndex *pindexRescan) {
	LogPrintf("Scanning blockchain for names to create fast index...\n");
	paliasdb->ReconstructAliasIndex(pindexRescan);
}

bool CAliasDB::ReconstructAliasIndex(CBlockIndex *pindexRescan) {
	CBlockIndex* pindex = pindexRescan;
	if(!HasReachedMainNetForkB2())
		return true;
	{
		TRY_LOCK(pwalletMain->cs_wallet, cs_trylock);
		while (pindex) {
			CBlock block;
			ReadBlockFromDisk(block, pindex, Params().GetConsensus());
			int nHeight = pindex->nHeight;
			uint256 txblkhash;

			BOOST_FOREACH(CTransaction& tx, block.vtx) {

				if (tx.nVersion != SYSCOIN_TX_VERSION)
					continue;

				vector<vector<unsigned char> > vvchArgs;
				int op, nOut;

				// decode the alias op
				bool o = DecodeAliasTx(tx, op, nOut, vvchArgs, -1);
				if (!o || !IsAliasOp(op))
					continue;

				const vector<unsigned char> &vchName = vvchArgs[0];


				if (!GetTransaction(tx.GetHash(), tx, Params().GetConsensus(), txblkhash, true))
					continue;

				// if name exists in DB, read it to verify
				vector<CAliasIndex> vtxPos;
				if (ExistsAlias(vchName)) {
					if (!ReadAlias(vchName, vtxPos))
						return error(
								"ReconstructAliasIndex() : failed to read from alias DB");
				}

				// rebuild the alias, store to DB
				CAliasIndex txName;
				txName.nHeight = nHeight;
				txName.txHash = tx.GetHash();

				PutToAliasList(vtxPos, txName);

				if (!WriteAlias(vchName, vtxPos))
					return error(
							"ReconstructAliasIndex() : failed to write to alias DB");

			
				LogPrintf(
						"RECONSTRUCT ALIAS: op=%s alias=%s hash=%s height=%d\n",
						aliasFromOp(op).c_str(), stringFromVch(vchName).c_str(),
						tx.GetHash().ToString().c_str(), nHeight);

			} /* TX */
			pindex = chainActive.Next(pindex);
		} /* BLOCK */
	} /* LOCK */
	return true;
}

int GetAliasExpirationDepth() {
	return 525600;
}



CScript RemoveAliasScriptPrefix(const CScript& scriptIn) {
	int op;
	vector<vector<unsigned char> > vvch;
	CScript::const_iterator pc = scriptIn.begin();

	if (!DecodeAliasScript(scriptIn, op, vvch, pc))
		throw runtime_error(
				"RemoveAliasScriptPrefix() : could not decode name script");
	return CScript(pc, scriptIn.end());
}

bool GetTxOfAlias(const vector<unsigned char> &vchName,
		CTransaction& tx) {
	vector<CAliasIndex> vtxPos;
	if (!paliasdb->ReadAlias(vchName, vtxPos) || vtxPos.empty())
		return false;
	CAliasIndex& txPos = vtxPos.back();
	int nHeight = txPos.nHeight;
	if (nHeight + GetAliasExpirationDepth()
			< chainActive.Tip()->nHeight) {
		string name = stringFromVch(vchName);
		LogPrintf("GetTxOfAlias(%s) : expired", name.c_str());
		return false;
	}

	uint256 hashBlock;
	if (!GetTransaction(txPos.txHash, tx, Params().GetConsensus(), hashBlock, true))
		return error("GetTxOfAlias() : could not read tx from disk");

	return true;
}

bool GetAliasAddress(const CTransaction& tx, std::string& strAddress) {
	int op, nOut = 0;
	vector<vector<unsigned char> > vvch;

	if (!DecodeAliasTx(tx, op, nOut, vvch, -1))
		return error("GetAliasAddress() : could not decode name tx.");

	const CTxOut& txout = tx.vout[nOut];

	const CScript& scriptPubKey = RemoveAliasScriptPrefix(txout.scriptPubKey);

	CTxDestination dest;
	ExtractDestination(scriptPubKey, dest);
	strAddress = CSyscoinAddress(dest).ToString();
	return true;
}

void GetAliasValue(const std::string& strName, std::string& strAddress) {
	try
	{
		vector<unsigned char> vchName = vchFromValue(strName);
		if (paliasdb && !paliasdb->ExistsAlias(vchName))
			throw runtime_error("Alias not found");

		// check for alias existence in DB
		vector<CAliasIndex> vtxPos;
		if (paliasdb && !paliasdb->ReadAlias(vchName, vtxPos))
			throw runtime_error("failed to read from alias DB");
		if (vtxPos.size() < 1)
			throw runtime_error("no alias result returned");

		// get transaction pointed to by alias
		uint256 blockHash;
		CTransaction tx;
		uint256 txHash = vtxPos.back().txHash;
		if (!GetTransaction(txHash, tx, Params().GetConsensus(), blockHash, true))
			throw runtime_error("failed to read transaction from disk");

		GetAliasAddress(tx, strAddress);
	}
	catch(...)
	{
		throw runtime_error("could not read alias");
	}
}

int IndexOfAliasOutput(const CTransaction& tx) {
	vector<vector<unsigned char> > vvch;

	int op;
	int nOut;
	bool good = DecodeAliasTx(tx, op, nOut, vvch, -1);

	if (!good)
		return -1;
	return nOut;
}

bool GetAliasOfTx(const CTransaction& tx, vector<unsigned char>& name) {
	if (tx.nVersion != SYSCOIN_TX_VERSION)
		return false;
	vector<vector<unsigned char> > vvchArgs;
	int op;
	int nOut;

	bool good = DecodeAliasTx(tx, op, nOut, vvchArgs, -1);
	if (!good)
		return error("GetAliasOfTx() : could not decode a syscoin tx");

	switch (op) {
	case OP_ALIAS_ACTIVATE:
	case OP_ALIAS_UPDATE:
		name = vvchArgs[0];
		return true;
	}
	return false;
}


bool DecodeAliasTx(const CTransaction& tx, int& op, int& nOut,
		vector<vector<unsigned char> >& vvch, int nHeight) {
	bool found = false;


	// Strict check - bug disallowed
	for (unsigned int i = 0; i < tx.vout.size(); i++) {
		const CTxOut& out = tx.vout[i];
		vector<vector<unsigned char> > vvchRead;
		if (DecodeAliasScript(out.scriptPubKey, op, vvchRead)) {
			nOut = i;
			found = true;
			vvch = vvchRead;
			break;
		}
	}
	if (!found)
		vvch.clear();

	return found && IsAliasOp(op);
}

bool DecodeAliasScript(const CScript& script, int& op,
		vector<vector<unsigned char> > &vvch) {
	CScript::const_iterator pc = script.begin();
	return DecodeAliasScript(script, op, vvch, pc);
}

bool DecodeAliasScript(const CScript& script, int& op,
		vector<vector<unsigned char> > &vvch, CScript::const_iterator& pc) {
	opcodetype opcode;
	vvch.clear();
	if (!script.GetOp(pc, opcode))
		return false;
	if (opcode < OP_1 || opcode > OP_16)
		return false;

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

	if ((op == OP_ALIAS_ACTIVATE && vvch.size() == 1)
			|| (op == OP_ALIAS_UPDATE && vvch.size() == 1))
		return true;
	return false;
}
void CreateRecipient(const CScript& scriptPubKey, CRecipient& recipient)
{
	recipient = {scriptPubKey, 0, false};
	CTxOut txout(recipient.nAmount,	recipient.scriptPubKey);
	recipient.nAmount = txout.GetDustThreshold(::minRelayTxFee);
}
void CreateFeeRecipient(const CScript& scriptPubKey, const vector<unsigned char>& data, CRecipient& recipient)
{
	CScript script;
	script += CScript() << data;
	CTxOut txout(0,	script);
	recipient = {scriptPubKey, 0, false};
	recipient.nAmount = txout.GetDustThreshold(::minRelayTxFee);
}
UniValue aliasnew(const UniValue& params, bool fHelp) {
	if (fHelp || params.size() != 2 )
		throw runtime_error(
				"aliasnew <aliasname> <value>\n"
						"<aliasname> alias name.\n"
						"<value> alias value, 1023 chars max.\n"
						"Perform a first update after an aliasnew reservation."
						+ HelpRequiringPassphrase());

	vector<unsigned char> vchName = vchFromString(params[0].get_str());
	int64_t rand = GetRand(std::numeric_limits<int64_t>::max());
	vector<unsigned char> vchValue;
	string strValue = params[1].get_str();
	if(params[0].get_str() == "SYS_RATES")
		boost::replace_all(strValue, "Qu", "\"");
	vchValue = vchFromString(strValue);

	if (vchValue.size() > MAX_VALUE_LENGTH)
		throw runtime_error("alias value cannot exceed 1023 bytes!");
	if (vchName.size() > MAX_NAME_LENGTH)
		throw runtime_error("alias name cannot exceed 255 bytes!");


	CSyscoinAddress myAddress = CSyscoinAddress(stringFromVch(vchName));
	if(myAddress.IsValid() && !myAddress.isAlias)
		throw runtime_error("alias name cannot be a syscoin address!");

	CWalletTx wtx;

	CTransaction tx;
	if (GetTxOfAlias(vchName, tx)) {
		error("aliasactivate() : this alias is already active with tx %s",
				tx.GetHash().GetHex().c_str());
		throw runtime_error("this alias is already active");
	}

	EnsureWalletIsUnlocked();

	// check for existing pending aliases
	if (ExistsInMempool(vchName, OP_ALIAS_ACTIVATE)) {
		throw runtime_error("there are pending operations on that alias");
	}
	

	CPubKey newDefaultKey;
	pwalletMain->GetKeyFromPool(newDefaultKey);
	CScript scriptPubKeyOrig;
	scriptPubKeyOrig = GetScriptForDestination(newDefaultKey.GetID());
	CScript scriptPubKey;
	scriptPubKey << CScript::EncodeOP_N(OP_ALIAS_ACTIVATE) << vchName << OP_2DROP;
	scriptPubKey += scriptPubKeyOrig;
	std::vector<unsigned char> vchPubKey(newDefaultKey.begin(), newDefaultKey.end());
	string strPubKey = HexStr(vchPubKey);

    // build alias
    CAliasIndex newAlias;
	newAlias.nHeight = chainActive.Tip()->nHeight;
	newAlias.vchPubKey = vchFromString(strPubKey);
	newAlias.vchValue = vchValue;

    vector<CRecipient> vecSend;
	CRecipient recipient;
	CreateRecipient(scriptPubKey, recipient);
	vecSend.push_back(recipient);
	CScript scriptData;
	const vector<unsigned char> &data = newAlias.Serialize();
	scriptData << OP_RETURN << data;
	CRecipient fee;
	CreateFeeRecipient(scriptData, data, fee);
	vecSend.push_back(fee);
	// send the tranasction
	SendMoneySyscoin(vecSend, recipient.nAmount + fee.nAmount, false, wtx);
	UniValue res(UniValue::VARR);
	res.push_back(wtx.GetHash().GetHex());
	return res;
}
UniValue aliasupdate(const UniValue& params, bool fHelp) {
	if (fHelp || 2 > params.size() || 3 < params.size())
		throw runtime_error(
				"aliasupdate <aliasname> <value> [<toalias>]\n"
						"Update and possibly transfer an alias.\n"
						"<aliasname> alias name.\n"
						"<value> alias value, 1023 chars max.\n"
                        "<toalias> receiver syscoin alias, if transferring alias.\n"
						+ HelpRequiringPassphrase());

	vector<unsigned char> vchName = vchFromString(params[0].get_str());
	vector<unsigned char> vchValue;
	string strValue = params[1].get_str();
	if(params[0].get_str() == "SYS_RATES")
		boost::replace_all(strValue, "Qu", "\"");
	vchValue = vchFromString(strValue);
	vector<unsigned char> vchPubKey;
	if (vchValue.size() > MAX_VALUE_LENGTH)
		throw runtime_error("alias value cannot exceed 1023 bytes!");
	CWalletTx wtx;
	CAliasIndex updateAlias;
	const CWalletTx* wtxIn;
	CScript scriptPubKeyOrig;
	string strPubKey;
    if (params.size() == 3) {
		string strAddress = params[2].get_str();
		CSyscoinAddress myAddress = CSyscoinAddress(strAddress);
		if (!myAddress.IsValid())
			throw runtime_error("Invalid syscoin address");
		if (!myAddress.isAlias)
			throw runtime_error("You must transfer an alias to another alias");

		// check for alias existence in DB
		vector<CAliasIndex> vtxPos;
		if (!paliasdb->ReadAlias(vchFromString(myAddress.aliasName), vtxPos))
			throw runtime_error("failed to read transfer to alias from alias DB");
		if (vtxPos.size() < 1)
			throw runtime_error("no result returned");
		CAliasIndex xferAlias = vtxPos.back();
		vchPubKey = xferAlias.vchPubKey;
		scriptPubKeyOrig = GetScriptForDestination(myAddress.Get());

	} else {
		CPubKey newDefaultKey;
		pwalletMain->GetKeyFromPool(newDefaultKey);
		scriptPubKeyOrig= GetScriptForDestination(newDefaultKey.GetID());
	}

	CScript scriptPubKey;
	scriptPubKey << CScript::EncodeOP_N(OP_ALIAS_UPDATE) << vchName << OP_2DROP;
	scriptPubKey += scriptPubKeyOrig;


	EnsureWalletIsUnlocked();

	CTransaction tx;
	if (!GetTxOfAlias(vchName, tx))
		throw runtime_error("could not find an alias with this name");

    if(!IsAliasMine(tx)) {
		throw runtime_error("This alias is not yours, you cannot update it.");
    }
	wtxIn = pwalletMain->GetWalletTx(tx.GetHash());
	if (wtxIn == NULL)
		throw runtime_error("this alias is not in your wallet");
	// check for existing pending aliases
	if (ExistsInMempool(vchName, OP_ALIAS_ACTIVATE) || ExistsInMempool(vchName, OP_ALIAS_UPDATE)) {
		throw runtime_error("there are pending operations on that alias");
	}
   // get the alias from DB
	CAliasIndex theAlias;
    vector<CAliasIndex> vtxPos;
    if (!paliasdb->ReadAlias(vchName, vtxPos) || vtxPos.empty())
        throw runtime_error("could not read alias from DB");
    theAlias = vtxPos.back();
	CAliasIndex copyAlias = theAlias;
	theAlias.ClearAlias();

	theAlias.nHeight = chainActive.Tip()->nHeight;
	if(copyAlias.vchValue != vchValue)
		theAlias.vchValue = vchValue;
	if(!vchPubKey.empty() && copyAlias.vchPubKey != vchPubKey)
		theAlias.vchPubKey = vchPubKey;

    vector<CRecipient> vecSend;
	CRecipient recipient;
	CreateRecipient(scriptPubKey, recipient);
	vecSend.push_back(recipient);
	const vector<unsigned char> &data = theAlias.Serialize();
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

UniValue aliaslist(const UniValue& params, bool fHelp) {
	if (fHelp || 1 < params.size())
		throw runtime_error("aliaslist [<aliasname>]\n"
				"list my own aliases.\n"
				"<aliasname> alias name to use as filter.\n");
	
	vector<unsigned char> vchName;

	if (params.size() == 1)
		vchName = vchFromValue(params[0]);

	vector<unsigned char> vchNameUniq;
	if (params.size() == 1)
		vchNameUniq = vchFromValue(params[0]);
	UniValue oRes(UniValue::VARR);
	map<vector<unsigned char>, int> vNamesI;
	map<vector<unsigned char>, UniValue> vNamesO;

	{
		uint256 blockHash;
		uint256 hash;
		CTransaction tx;
	
		uint64_t nHeight;
		BOOST_FOREACH(PAIRTYPE(const uint256, CWalletTx)& item, pwalletMain->mapWallet) {
			// get txn hash, read txn index
			hash = item.second.GetHash();
			const CWalletTx &wtx = item.second;
			// skip non-syscoin txns
			if (wtx.nVersion != SYSCOIN_TX_VERSION)
				continue;

			// decode txn, skip non-alias txns
			vector<vector<unsigned char> > vvch;
			int op, nOut;
			if (!DecodeAliasTx(wtx, op, nOut, vvch, -1) || !IsAliasOp(op))
				continue;

			// get the txn alias name
			if (!GetAliasOfTx(wtx, vchName))
				continue;

			// skip this alias if it doesn't match the given filter value
			if (vchNameUniq.size() > 0 && vchNameUniq != vchName)
				continue;
			// get last active name only
			if (vNamesI.find(vchName) != vNamesI.end() && (nHeight < vNamesI[vchName] || vNamesI[vchName] < 0))
				continue;

			vector<CAliasIndex> vtxPos;
			if (!paliasdb->ReadAlias(vchName, vtxPos) || vtxPos.empty())
				continue;
			CAliasIndex alias = vtxPos.back();	
			if (!GetTransaction(alias.txHash, tx, Params().GetConsensus(), blockHash, true))
				continue;
			if(!IsAliasMine(tx))
				continue;
			nHeight = alias.nHeight;
		
			int expired = 0;
			int expires_in = 0;
			int expired_block = 0;
			// build the output UniValue
			UniValue oName(UniValue::VOBJ);
			oName.push_back(Pair("name", stringFromVch(vchName)));
			oName.push_back(Pair("value", stringFromVch(alias.vchValue)));
			expired_block = nHeight + GetAliasExpirationDepth();
			if(nHeight + GetAliasExpirationDepth() - chainActive.Tip()->nHeight <= 0)
			{
				expired = 1;
			}  
			if(expired == 0)
			{
				expires_in = nHeight + GetAliasExpirationDepth() - chainActive.Tip()->nHeight;
			}
			oName.push_back(Pair("expires_in", expires_in));
			oName.push_back(Pair("expires_on", expired_block));
			oName.push_back(Pair("expired", expired));
			vNamesI[vchName] = nHeight;
			vNamesO[vchName] = oName;					

		}
	}

	BOOST_FOREACH(const PAIRTYPE(vector<unsigned char>, UniValue)& item, vNamesO)
		oRes.push_back(item.second);

	return oRes;
}

/**
 * [aliasinfo description]
 * @param  params [description]
 * @param  fHelp  [description]
 * @return        [description]
 */
UniValue aliasinfo(const UniValue& params, bool fHelp) {
	if (fHelp || 1 != params.size())
		throw runtime_error("aliasinfo <aliasname>\n"
				"Show values of an alias.\n");
	vector<unsigned char> vchName = vchFromValue(params[0]);
	CTransaction tx;
	UniValue oShowResult(UniValue::VOBJ);

	{

		// check for alias existence in DB
		vector<CAliasIndex> vtxPos;
		if (!paliasdb->ReadAlias(vchName, vtxPos))
			throw runtime_error("failed to read from alias DB");
		if (vtxPos.size() < 1)
			throw runtime_error("no result returned");

		// get transaction pointed to by alias
		uint256 blockHash;
		uint256 txHash = vtxPos.back().txHash;
		if (!GetTransaction(txHash, tx, Params().GetConsensus(), blockHash, true))
			throw runtime_error("failed to read transaction from disk");

		UniValue oName(UniValue::VOBJ);
		vector<unsigned char> vchValue;
		uint64_t nHeight;
		int expired = 0;
		int expires_in = 0;
		int expired_block = 0;
		nHeight = vtxPos.back().nHeight;
		oName.push_back(Pair("name", stringFromVch(vchName)));
		string value = stringFromVch(vchValue);
		oName.push_back(Pair("value", stringFromVch(vtxPos.back().vchValue)));
		oName.push_back(Pair("txid", tx.GetHash().GetHex()));
		string strAddress = "";
		GetAliasAddress(tx, strAddress);
		oName.push_back(Pair("address", strAddress));
		bool fAliasMine = IsAliasMine(tx)? true:  false;
		oName.push_back(Pair("isaliasmine", fAliasMine));
		bool fMine = pwalletMain->IsMine(tx)? true:  false;
		oName.push_back(Pair("ismine", fMine));
        oName.push_back(Pair("lastupdate_height", nHeight));
		expired_block = nHeight + GetAliasExpirationDepth();
		if(nHeight + GetAliasExpirationDepth() - chainActive.Tip()->nHeight <= 0)
		{
			expired = 1;
		}  
		if(expired == 0)
		{
			expires_in = nHeight + GetCertExpirationDepth() - chainActive.Tip()->nHeight;
		}
		oName.push_back(Pair("expires_in", expires_in));
		oName.push_back(Pair("expires_on", expired_block));
		oName.push_back(Pair("expired", expired));
		oShowResult = oName;
	}
	return oShowResult;
}

/**
 * [aliashistory description]
 * @param  params [description]
 * @param  fHelp  [description]
 * @return        [description]
 */
UniValue aliashistory(const UniValue& params, bool fHelp) {
	if (fHelp || 1 != params.size())
		throw runtime_error("aliashistory <aliasname>\n"
				"List all stored values of an alias.\n");
	UniValue oRes(UniValue::VARR);
	vector<unsigned char> vchName = vchFromValue(params[0]);
	string name = stringFromVch(vchName);

	{
		vector<CAliasIndex> vtxPos;
		if (!paliasdb->ReadAlias(vchName, vtxPos) || vtxPos.empty())
			throw runtime_error("failed to read from alias DB");

		CAliasIndex txPos2;
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
			UniValue oName(UniValue::VOBJ);
			vector<unsigned char> vchValue;
			uint64_t nHeight;
			nHeight = txPos2.nHeight;
			oName.push_back(Pair("name", name));
			string value = stringFromVch(vchValue);
			oName.push_back(Pair("value", stringFromVch(txPos2.vchValue)));
			oName.push_back(Pair("txid", tx.GetHash().GetHex()));
			string strAddress = "";
			GetAliasAddress(tx, strAddress);
			oName.push_back(Pair("address", strAddress));
            oName.push_back(Pair("lastupdate_height", nHeight));
			expired_block = nHeight + GetAliasExpirationDepth();
			if(nHeight + GetAliasExpirationDepth() - chainActive.Tip()->nHeight <= 0)
			{
				expired = 1;
			}  
			if(expired == 0)
			{
				expires_in = nHeight + GetCertExpirationDepth() - chainActive.Tip()->nHeight;
			}
			oName.push_back(Pair("expires_in", expires_in));
			oName.push_back(Pair("expires_on", expired_block));
			oName.push_back(Pair("expired", expired));
			oRes.push_back(oName);
		}
	}
	return oRes;
}

/**
 * [aliasfilter description]
 * @param  params [description]
 * @param  fHelp  [description]
 * @return        [description]
 */
UniValue aliasfilter(const UniValue& params, bool fHelp) {
	if (fHelp || params.size() > 5)
		throw runtime_error(
				"aliasfilter [[[[[regexp] maxage=36000] from=0] nb=0] stat]\n"
						"scan and filter aliases\n"
						"[regexp] : apply [regexp] on aliases, empty means all aliases\n"
						"[maxage] : look in last [maxage] blocks\n"
						"[from] : show results from number [from]\n"
						"[nb] : show [nb] results, 0 means all\n"
						"[stat] : show some stats instead of results\n"
						"aliasfilter \"\" 5 # list aliases updated in last 5 blocks\n"
						"aliasfilter \"^name\" # list all aliases starting with \"name\"\n"
						"aliasfilter 36000 0 0 stat # display stats (number of names) on active aliases\n");

	string strRegexp;
	int nFrom = 0;
	int nNb = 0;
	int nMaxAge = GetAliasExpirationDepth();
	bool fStat = false;
	int nCountFrom = 0;
	int nCountNb = 0;
	/* when changing this to match help, review bitcoinrpc.cpp RPCConvertValues() */
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

	UniValue oRes(UniValue::VARR);

	vector<unsigned char> vchName;
	vector<pair<vector<unsigned char>, CAliasIndex> > nameScan;
	if (!paliasdb->ScanNames(vchName, 100000000, nameScan))
		throw runtime_error("scan failed");
	// regexp
	using namespace boost::xpressive;
	smatch nameparts;
	sregex cregex = sregex::compile(strRegexp);
	pair<vector<unsigned char>, CAliasIndex> pairScan;
	BOOST_FOREACH(pairScan, nameScan) {
		string name = stringFromVch(pairScan.first);
		if (strRegexp != "" && !regex_search(name, nameparts, cregex))
			continue;

		CAliasIndex txName = pairScan.second;
		int nHeight = txName.nHeight;

		// max age
		if (nMaxAge != 0 && chainActive.Tip()->nHeight - nHeight >= nMaxAge)
			continue;

		// from limits
		nCountFrom++;
		if (nCountFrom < nFrom + 1)
			continue;


		int expired = 0;
		int expires_in = 0;
		int expired_block = 0;
		UniValue oName(UniValue::VOBJ);
		oName.push_back(Pair("name", name));
		CTransaction tx;
		uint256 blockHash;
		uint256 txHash = txName.txHash;
		if (!GetTransaction(txHash, tx, Params().GetConsensus(), blockHash, true))
			continue;

		oName.push_back(Pair("value", stringFromVch(txName.vchValue)));
		oName.push_back(Pair("txid", txHash.GetHex()));
        oName.push_back(Pair("lastupdate_height", nHeight));
		expired_block = nHeight + GetAliasExpirationDepth();
        if(nHeight + GetAliasExpirationDepth() - chainActive.Tip()->nHeight <= 0)
		{
			expired = 1;
		}  
		if(expired == 0)
		{
			expires_in = nHeight + GetCertExpirationDepth() - chainActive.Tip()->nHeight;
		}
		oName.push_back(Pair("expires_in", expires_in));
		oName.push_back(Pair("expires_on", expired_block));
		oName.push_back(Pair("expired", expired));

		
		oRes.push_back(oName);

		nCountNb++;
		// nb limits
		if (nNb > 0 && nCountNb >= nNb)
			break;
	}

	if (fStat) {
		UniValue oStat(UniValue::VOBJ);
		oStat.push_back(Pair("blocks", (int) chainActive.Tip()->nHeight));
		oStat.push_back(Pair("count", (int) oRes.size()));
		return oStat;
	}

	return oRes;
}

/**
 * [aliasscan description]
 * @param  params [description]
 * @param  fHelp  [description]
 * @return        [description]
 */
UniValue aliasscan(const UniValue& params, bool fHelp) {
	if (fHelp || 2 > params.size())
		throw runtime_error(
				"aliasscan [<start-name>] [<max-returned>]\n"
						"scan all aliases, starting at start-name and returning a maximum number of entries (default 500)\n");

	vector<unsigned char> vchName;
	int nMax = 500;
	if (params.size() > 0)
		vchName = vchFromValue(params[0]);
	if (params.size() > 1) {
		nMax = params[1].get_int();
	}

	UniValue oRes(UniValue::VARR);

	vector<pair<vector<unsigned char>, CAliasIndex> > nameScan;
	if (!paliasdb->ScanNames(vchName, nMax, nameScan))
		throw runtime_error("scan failed");

	pair<vector<unsigned char>, CAliasIndex> pairScan;
	BOOST_FOREACH(pairScan, nameScan) {
		UniValue oName(UniValue::VOBJ);
		string name = stringFromVch(pairScan.first);
		oName.push_back(Pair("name", name));
		CTransaction tx;
		CAliasIndex txName = pairScan.second;
		uint256 blockHash;
		int expired = 0;
		int expires_in = 0;
		int expired_block = 0;
		int nHeight = txName.nHeight;
		if (!GetTransaction(txName.txHash, tx, Params().GetConsensus(), blockHash, true))
			continue;

		oName.push_back(Pair("txid", txName.txHash.GetHex()));
		oName.push_back(Pair("value", stringFromVch(txName.vchValue)));
        oName.push_back(Pair("lastupdate_height", nHeight));
		expired_block = nHeight + GetAliasExpirationDepth();
		if(nHeight + GetAliasExpirationDepth() - chainActive.Tip()->nHeight <= 0)
		{
			expired = 1;
		}  
		if(expired == 0)
		{
			expires_in = nHeight + GetCertExpirationDepth() - chainActive.Tip()->nHeight;
		}
		oName.push_back(Pair("expires_in", expires_in));
		oName.push_back(Pair("expires_on", expired_block));
		oName.push_back(Pair("expired", expired));
		
		oRes.push_back(oName);
	}

	return oRes;
}