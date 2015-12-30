#ifndef SYSCOIN_TEST_TEST_SYSCOIN_SERVICES_H
#define SYSCOIN_TEST_TEST_SYSCOIN_SERVICES_H

#include <stdio.h>
#include <univalue.h>
using namespace std;
/** Testing syscoin services setup that configures a complete environment with 3 nodes.
 */
UniValue CallRPC(const string &dataDir, const string& commandWithArgs);
void StartNode(const string &dataDir);
void StartNodes();
void StopNodes();
void GenerateBlocks(int nBlocks, const string& node="node1");
string CallExternal(string &cmd);
void AliasNew(const string& node, const string& aliasname, const string& aliasdata);
void AliasUpdate(const string& node, const string& aliasname, const string& aliasdata);
const string CertNew(const string& node, const string& title, const string& data, bool privateData=false);
void CertUpdate(const string& node, const string& guid, const string& title, const string& data, bool privateData=false);
void CertTransfer(const string& node, const string& guid, const string& toalias);
const string MessageNew(const string& fromnode, const string& tonode, const string& title, const string& data, const string& fromalias, const string& toalias);
// SYSCOIN testing setup
struct SyscoinTestingSetup {
    SyscoinTestingSetup();
    ~SyscoinTestingSetup();
};
struct BasicSyscoinTestingSetup {
    BasicSyscoinTestingSetup();
    ~BasicSyscoinTestingSetup();
};
#endif
