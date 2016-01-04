#include "test/test_syscoin_services.h"
#include "utiltime.h"
#include "rpcserver.h"
#include <boost/test/unit_test.hpp>
BOOST_FIXTURE_TEST_SUITE (syscoin_escrow_tests, BasicSyscoinTestingSetup)

BOOST_AUTO_TEST_CASE (generate_escrow_release)
{
	GenerateBlocks(200);
	GenerateBlocks(200, "node2");
	GenerateBlocks(200, "node3");
	AliasNew("node1", "buyeralias", "changeddata1");
	AliasNew("node2", "selleralias", "changeddata2");
	AliasNew("node3", "arbiteralias", "changeddata3");
	string qty = "3";
	string message = "paymentmessage";
	string offerguid = OfferNew("node2", "selleralias", "category", "title", "100", "0.05", "description", "USD");
	string guid = EscrowNew("node1", offerguid, qty, message, "arbiteralias");
	EscrowRelease("node1", guid);
	EscrowClaimRelease("node2", guid);
}
/*BOOST_AUTO_TEST_CASE (generate_escrowrefund_seller)
{
	string qty = "4";
	string offerguid = OfferNew("node2", "selleralias", "category", "title", "100", "1.22", "description", "CAD");
	string guid = EscrowNew("node1", offerguid, qty, message, "arbiteralias");
	RefundEscrow("node2", guid);
	ClaimEscrowRefund("node1", guid);
}
BOOST_AUTO_TEST_CASE (generate_escrowrefund_arbiter)
{
	string qty = "5";
	string offerguid = OfferNew("node2", "selleralias", "category", "title", "100", "0.25", "description", "EUR");
	string guid = EscrowNew("node1", offerguid, qty, message, "arbiteralias");
	RefundEscrow("node3", guid);
	ClaimEscrowRefund("node1", guid);
}
BOOST_AUTO_TEST_CASE (generate_escrowrefund_invalid)
{
	string qty = "2";
	string offerguid = OfferNew("node2", "selleralias", "category", "title", "100", "0.001", "description", "BTC");
	string guid = EscrowNew("node1", offerguid, qty, message, "arbiteralias");
	// try to claim refund even if not refunded
	BOOST_CHECK_THROW(ClaimEscrowRefund("node1", guid, runtime_error));
	// buyer cant refund
	BOOST_CHECK_THROW(RefundEscrow("node1", guid), runtime_error));
	RefundEscrow("node2", guid);
	// try to release already refunded escrow
	BOOST_CHECK_THROW(EscrowRelease("node1", guid), runtime_error));
	// cant refund already refunded escrow
	BOOST_CHECK_THROW(RefundEscrow("node3", guid, runtime_error));
	BOOST_CHECK_THROW(RefundEscrow("node2", guid, runtime_error));
	// noone other than buyer can claim refund
	BOOST_CHECK_THROW(ClaimEscrowRefund("node3", guid, runtime_error));
	BOOST_CHECK_THROW(ClaimEscrowRefund("node2", guid, runtime_error));
	ClaimEscrowRefund("node1", guid);
	// cant inititate another refund after claimed already
	BOOST_CHECK_THROW(RefundEscrow("node2", guid, runtime_error));
}*/
BOOST_AUTO_TEST_CASE (generate_escrowrelease_invalid)
{
	string qty = "4";
	string offerguid = OfferNew("node2", "selleralias", "category", "title", "100", "1.45", "description", "SYS");
	string guid = EscrowNew("node1", offerguid, qty, "message", "arbiteralias");
	// try to claim release even if not released
	BOOST_CHECK_THROW(CallRPC("node2", "escrowclaimrelease " + guid), runtime_error);
	// seller cant release buyers funds
	BOOST_CHECK_THROW(CallRPC("node2", "escrowrelease " + guid), runtime_error);
	EscrowRelease("node1", guid);
	// try to refund already released escrow
	//BOOST_CHECK_THROW(RefundEscrow("node2", guid), runtime_error);
	// cant release already released escrow
	BOOST_CHECK_THROW(CallRPC("node1", "escrowrelease " + guid), runtime_error);
	// noone other than seller can claim release
	BOOST_CHECK_THROW(CallRPC("node3", "escrowclaimrelease " + guid), runtime_error);
	BOOST_CHECK_THROW(CallRPC("node1", "escrowclaimrelease " + guid), runtime_error);
	EscrowClaimRelease("node2", guid);
	// cant inititate another release after claimed already
	BOOST_CHECK_THROW(CallRPC("node1", "escrowrelease " + guid), runtime_error);
}
BOOST_AUTO_TEST_CASE (generate_escrowrelease_arbiter)
{
	string qty = "1";
	string offerguid = OfferNew("node2", "selleralias", "category", "title", "100", "0.05", "description", "GBP");
	string guid = EscrowNew("node1", offerguid, qty, "message", "arbiteralias");
	EscrowRelease("node3", guid);
	EscrowClaimRelease("node2", guid);
}
BOOST_AUTO_TEST_SUITE_END ()