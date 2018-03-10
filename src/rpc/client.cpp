// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Syscoin Core developers
// Copyright (c) 2014-2017 The Syscoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "rpc/client.h"
#include "rpc/protocol.h"
#include "util.h"

#include <set>
#include <stdint.h>

#include <boost/algorithm/string/case_conv.hpp> // for to_lower()
#include <univalue.h>

class CRPCConvertParam
{
public:
    std::string methodName; //!< method whose params want conversion
    int paramIdx;           //!< 0-based idx of param to convert
    std::string paramName;  //!< parameter name
};
/**
 * Specifiy a (method, idx, name) here if the argument is a non-string RPC
 * argument and needs to be converted from JSON.
 *
 * @note Parameter indexes start from 0.
 */
static const CRPCConvertParam vRPCConvertParams[] =
{
    { "setmocktime", 0, "timestamp" },
    { "generate", 0, "nblocks" },
    { "generate", 1, "maxtries" },
    { "generatetoaddress", 0, "nblocks" },
    { "generatetoaddress", 2, "maxtries" },
    { "getnetworkhashps", 0, "nblocks" },
    { "getnetworkhashps", 1, "height" },
    { "sendtoaddress", 1, "amount" },
    { "sendtoaddress", 4, "subtractfeefromamount" },
    { "sendtoaddress", 5, "use_is" },
    { "sendtoaddress", 6, "use_ps" },
    { "instantsendtoaddress", 1, "address" },
    { "instantsendtoaddress", 4, "comment_to" },
    { "settxfee", 0, "amount" },
    { "getreceivedbyaddress", 1, "minconf" },
    { "getreceivedbyaddress", 2, "addlockconf" },
    { "getreceivedbyaccount", 1, "minconf" },
    { "getreceivedbyaccount", 2, "addlockconf" },
    { "listaddressbalances", 0, "minamount" },
    { "listreceivedbyaddress", 0, "minconf" },
    { "listreceivedbyaddress", 1, "addlockconf" },
    { "listreceivedbyaddress", 2, "include_empty" },
    { "listreceivedbyaddress", 3, "include_watchonly" },
    { "listreceivedbyaccount", 0, "minconf" },
    { "listreceivedbyaccount", 1, "addlockconf" },
    { "listreceivedbyaccount", 2, "include_empty" },
    { "listreceivedbyaccount", 3, "include_watchonly" },
    { "getbalance", 1, "minconf" },
    { "getbalance", 2, "addlockconf" },
    { "getbalance", 3, "include_watchonly" },
    { "getchaintips", 0, "count" },
    { "getchaintips", 1, "branchlen" },
    { "getblockhash", 0, "height" },
    { "getsuperblockbudget", 0, "index" },
    { "waitforblockheight", 0, "height" },
    { "waitforblockheight", 1, "timeout" },
    { "waitforblock", 1, "timeout" },
    { "waitfornewblock", 0, "timeout" },
    { "move", 2, "amount" },
    { "move", 3, "minconf" },
    { "sendfrom", 2, "amount" },
    { "sendfrom", 3, "minconf" },
    { "sendfrom", 4, "addlockconf" },
    { "listtransactions", 1, "count" },
    { "listtransactions", 2, "skip" },
    { "listtransactions", 3, "include_watchonly" },
    { "listaccounts", 0, "minconf" },
    { "listaccounts", 1, "addlockconf" },
    { "listaccounts", 2, "include_watchonly" },
    { "walletpassphrase", 1, "timeout" },
    { "walletpassphrase", 2, "mixingonly" },
    { "getblocktemplate", 0, "template_request" },
    { "listsinceblock", 1, "target_confirmations" },
    { "listsinceblock", 2, "include_watchonly" },
    { "sendmany", 1, "amounts" },
    { "sendmany", 2, "minconf" },
    { "sendmany", 3, "addlockconf" },
    { "sendmany", 5, "subtractfeefromamount" },
    { "sendmany", 6, "use_is" },
    { "sendmany", 7, "use_ps" },
    { "addmultisigaddress", 0, "nrequired" },
    { "addmultisigaddress", 1, "keys" },
    { "createmultisig", 0, "nrequired" },
    { "createmultisig", 1, "keys" },
    { "listunspent", 0, "minconf" },
    { "listunspent", 1, "maxconf" },
    { "listunspent", 2, "addresses" },
    { "listunspent", 3, "include_unsafe" },
    { "getblock", 1, "verbose" },
    { "getblockheader", 1, "verbose" },
    { "getblockheaders", 1, "count" },
    { "getblockheaders", 2, "verbose" },
    { "gettransaction", 1, "include_watchonly" },
    { "getrawtransaction", 1, "verbose" },
    { "createrawtransaction", 0, "inputs" },
    { "createrawtransaction", 1, "outputs" },
    { "createrawtransaction", 2, "locktime" },
    { "signrawtransaction", 1, "prevtxs" },
    { "signrawtransaction", 2, "privkeys" },
    { "sendrawtransaction", 1, "allowhighfees" },
    { "sendrawtransaction", 2, "instantsend" },    
    { "fundrawtransaction", 1, "options" },
    { "gettxout", 1, "n" },
    { "gettxout", 2, "include_mempool" },
    { "gettxoutproof", 0, "txids" },
    { "lockunspent", 0, "unlock" },
    { "lockunspent", 1, "transactions" },
    { "importprivkey", 2, "rescan" },
    { "importelectrumwallet", 1, "index" },
    { "importaddress", 2, "rescan" },
    { "importaddress", 3, "p2sh" },
    { "importpubkey", 2, "rescan" },
    { "importmulti", 0, "requests" },
    { "importmulti", 1, "options" },
    { "verifychain", 0, "checklevel" },
    { "verifychain", 1, "nblocks" },
    { "pruneblockchain", 0, "height" },
    { "keypoolrefill", 0, "newsize" },
    { "getrawmempool", 0, "verbose" },
    { "estimatefee", 0, "nblocks" },
    { "estimatepriority", 0, "nblocks" },
    { "estimatesmartfee", 0, "nblocks" },
    { "estimatesmartpriority", 0, "nblocks" },
    { "prioritisetransaction", 1, "priority_delta" },
    { "prioritisetransaction", 2, "fee_delta" },
    { "setban", 2, "bantime" },
    { "setban", 3, "absolute" },
    { "setbip69enabled", 0, "enabled" },
    { "setnetworkactive", 0, "state" },
    { "getmempoolancestors", 1, "verbose" },
    { "getmempooldescendants", 1, "verbose" },
    { "spork", 1, "value" },
    { "voteraw", 1, "tx_index" },
    { "voteraw", 5, "time" },
    { "getblockhashes", 0, "high"},
    { "getblockhashes", 1, "low" },
    { "getspentinfo", 0, "json" },
    { "getaddresstxids", 0, "addresses" },
    { "getaddressbalance", 0, "addresses" },
    { "getaddressdeltas", 0, "addresses" },
    { "getaddressutxos", 0, "addresses" },
    { "getaddressmempool", 0, "addresses" },
    // Echo with conversion (For testing only)
    { "echojson", 0, "arg0" },
    { "echojson", 1, "arg1" },
    { "echojson", 2, "arg2" },
    { "echojson", 3, "arg3" },
    { "echojson", 4, "arg4" },
    { "echojson", 5, "arg5" },
    { "echojson", 6, "arg6" },
    { "echojson", 7, "arg7" },
    { "echojson", 8, "arg8" },
    { "echojson", 9, "arg9" },
	{ "aliaspay", 1, "addresses" },
	{ "aliaspay", 2, "instantsend" },
	{ "aliaspay", 3, "subtractfeefromamount" },
	{ "syscoinsendrawtransaction", 1, "allowhighfees" },
	{ "syscoinsendrawtransaction", 2, "instantsend" },
	{ "escrowcreaterawtransaction", 2, "inputs" },
	{ "aliasupdatewhitelist", 1, "aliases" },
	{ "aliasnew", 2, "accept_transfers_flags" },
	{ "aliasnew", 3, "expire_timestamp" },
	{ "syscointxfund", 1, "addresses" },
	{ "syscointxfund", 2, "instantsend" },
	{ "aliasbalance", 1, "instantsend" },
	{ "aliasupdate", 3, "accept_transfers_flags" },
	{ "aliasupdate", 4, "expire_timestamp" },
	{ "certtransfer", 3, "accessflags" },
	{ "offernew", 3, "quantity" },
	{ "offernew", 4, "price" },
	{ "offernew", 9, "private" },
	{ "offernew", 10, "units" },
	{ "offernew", 12, "auction_expires" },
	{ "offernew", 13, "auction_reserve" },
	{ "offernew", 14, "auction_require_witness" },
	{ "offernew", 15, "auction_deposit" },
	{ "offerlink", 2, "commission" },
	{ "offerupdate", 4, "quantity" },
	{ "offerupdate", 5, "price" },
	{ "offerupdate", 8, "private" },
	{ "offerupdate", 10, "commission" },
	{ "offerupdate", 13, "auction_expires" },
	{ "offerupdate", 14, "auction_reserve" },
	{ "offerupdate", 15, "auction_require_witness" },
	{ "offerupdate", 16, "auction_deposit" },
	{ "escrowbid", 2, "bid_in_payment_option" },
	{ "escrowbid", 3, "bid_in_offer_currency" },
	{ "escrownew", 0, "getamountandaddress" },
	{ "escrownew", 4, "quantity" },
	{ "escrownew", 5, "buynow" },
	{ "escrownew", 6, "total_in_payment_option" },
	{ "escrownew", 7, "shipping_amount" },
	{ "escrownew", 8, "network_fee" },
	{ "escrownew", 9, "arbiter_fee" },
	{ "escrownew", 10, "witness_fee" },
	{ "escrownew", 13, "bid_in_payment_option" },
	{ "escrownew", 14, "bid_in_offer_currency" },
	{ "escrowfeedback", 3, "rating" },
	{ "assetallocationinfo", 2, "getinputs" },
	{ "assetallocationsend", 2, "inputs" },
	{ "assetnew", 4, "precision" },
	{ "assetnew", 5, "use_inputranges" },
	{ "assetnew", 8, "interest_rate" },
	{ "assetnew", 9, "can_adjust_interest_rate" },
	{ "assetupdate", 4, "interest_rate" },
	{ "assetinfo", 1, "getinputs" },
	{ "assetsend", 2, "inputs" }
    { "aliasstat", 0}
};

class CRPCConvertTable
{
private:
    std::set<std::pair<std::string, int>> members;
    std::set<std::pair<std::string, std::string>> membersByName;

public:
    CRPCConvertTable();

    bool convert(const std::string& method, int idx) {
        return (members.count(std::make_pair(method, idx)) > 0);
    }
    bool convert(const std::string& method, const std::string& name) {
        return (membersByName.count(std::make_pair(method, name)) > 0);
    }
};

CRPCConvertTable::CRPCConvertTable()
{
    const unsigned int n_elem =
        (sizeof(vRPCConvertParams) / sizeof(vRPCConvertParams[0]));

    for (unsigned int i = 0; i < n_elem; i++) {
        members.insert(std::make_pair(vRPCConvertParams[i].methodName,
                                      vRPCConvertParams[i].paramIdx));
        membersByName.insert(std::make_pair(vRPCConvertParams[i].methodName,
                                            vRPCConvertParams[i].paramName));
    }
}

static CRPCConvertTable rpcCvtTable;

/** Non-RFC4627 JSON parser, accepts internal values (such as numbers, true, false, null)
 * as well as objects and arrays.
 */
UniValue ParseNonRFCJSONValue(const std::string& strVal)
{
    UniValue jVal;
    if (!jVal.read(std::string("[")+strVal+std::string("]")) ||
        !jVal.isArray() || jVal.size()!=1)
        throw std::runtime_error(std::string("Error parsing JSON:")+strVal);
    return jVal[0];
}

UniValue RPCConvertValues(const std::string &strMethod, const std::vector<std::string> &strParams)
{
    UniValue params(UniValue::VARR);

    for (unsigned int idx = 0; idx < strParams.size(); idx++) {
        const std::string& strVal = strParams[idx];

        if (!rpcCvtTable.convert(strMethod, idx)) {
            // insert string value directly
            params.push_back(strVal);
        } else {
            // parse string as JSON, insert bool/number/object/etc. value
            params.push_back(ParseNonRFCJSONValue(strVal));
        }
    }

    return params;
}

UniValue RPCConvertNamedValues(const std::string &strMethod, const std::vector<std::string> &strParams)
{
    UniValue params(UniValue::VOBJ);

    for (const std::string &s: strParams) {
        size_t pos = s.find("=");
        if (pos == std::string::npos) {
            throw(std::runtime_error("No '=' in named argument '"+s+"', this needs to be present for every argument (even if it is empty)"));
        }

        std::string name = s.substr(0, pos);
        std::string value = s.substr(pos+1);

        if (!rpcCvtTable.convert(strMethod, name)) {
            // insert string value directly
            params.pushKV(name, value);
        } else {
            // parse string as JSON, insert bool/number/object/etc. value
            params.pushKV(name, ParseNonRFCJSONValue(value));
        }
    }

    return params;
}
