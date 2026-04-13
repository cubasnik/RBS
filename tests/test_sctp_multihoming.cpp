#include "../src/common/sctp_socket.h"
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

using namespace rbs::net;

// Helper to test single address binding (baseline)
void testSingleAddressBind() {
    std::cout << "Testing single address bind..." << std::endl;
    SctpSocket sock("test-single");
    
    bool ok = sock.bind(0);
    // On Windows with fallback, should succeed
    // On Linux with native SCTP, should succeed
    assert(ok);
    assert(sock.localPort() > 0);
    
    std::cout << "  ✓ Single address bind successful on port " << sock.localPort() << std::endl;
}

// Helper to test multi-address binding
void testMultiAddressBind() {
    std::cout << "Testing multi-address bind..." << std::endl;
    SctpSocket sock("test-multi");
    
    std::vector<std::string> addrs = {"127.0.0.1", "127.0.0.2"};
    // Note: 127.0.0.2 might not be configured on all systems
    // On fallback systems (Windows), only first address is used
    // On Linux with proper loopback aliases, both should work
    
    bool ok = sock.bindMulti(addrs, 0);
    // Should succeed even on fallback (with warning)
    assert(ok);
    assert(sock.localPort() > 0);
    
    std::cout << "  ✓ Multi-address bind returned ok, port " << sock.localPort() << std::endl;
}

// Test empty address list rejection
void testEmptyAddressList() {
    std::cout << "Testing empty address list rejection..." << std::endl;
    SctpSocket sock("test-empty");
    
    std::vector<std::string> empty;
    bool ok = sock.bindMulti(empty, 0);
    assert(!ok);  // Should reject empty list
    
    std::cout << "  ✓ Empty address list correctly rejected" << std::endl;
}

// Test single-address connect (baseline)
void testSingleAddressConnect() {
    std::cout << "Testing single address connect..." << std::endl;
    SctpSocket sock("test-single-connect");
    
    sock.bind(0);
    bool ok = sock.connect("127.0.0.1", 1234);
    // On fallback, connect is no-op; on native SCTP, should work for loopback
    assert(ok);
    
    assert(sock.remoteAddrsCount() == 1);
    assert(sock.primaryRemoteIdx() == 0);
    
    std::cout << "  ✓ Single address connect ok, stored " 
              << sock.remoteAddrsCount() << " remote address(es)" << std::endl;
}

// Test multi-address connect
void testMultiAddressConnect() {
    std::cout << "Testing multi-address connect..." << std::endl;
    SctpSocket sock("test-multi-connect");
    
    sock.bind(0);
    
    std::vector<std::pair<std::string, uint16_t>> remotes = {
        {"127.0.0.1", 2234},
        {"127.0.0.1", 2235},
        {"127.0.0.1", 2236}
    };
    
    bool ok = sock.connectMulti(remotes, 1);  // Make second address primary
    assert(ok);
    assert(sock.remoteAddrsCount() == 3);
    assert(sock.primaryRemoteIdx() == 1);
    
    std::cout << "  ✓ Multi-address connect ok, stored " 
              << sock.remoteAddrsCount() << " remote address(es), primary index=" 
              << sock.primaryRemoteIdx() << std::endl;
}

// Test primary path changes
void testPrimaryPathSwitch() {
    std::cout << "Testing primary path switch..." << std::endl;
    SctpSocket sock("test-path-switch");
    
    sock.bind(0);
    
    std::vector<std::pair<std::string, uint16_t>> remotes = {
        {"127.0.0.1", 3234},
        {"127.0.0.1", 3235},
        {"127.0.0.1", 3236}
    };
    
    sock.connectMulti(remotes, 0);
    assert(sock.primaryRemoteIdx() == 0);
    
    // Switch to second address
    bool ok = sock.setPrimaryPath(2);
    assert(ok);
    assert(sock.primaryRemoteIdx() == 2);
    
    // Try to switch back
    ok = sock.setPrimaryPath(0);
    assert(ok);
    assert(sock.primaryRemoteIdx() == 0);
    
    std::cout << "  ✓ Primary path switch successful" << std::endl;
}

// Test invalid primary index rejection
void testInvalidPathIndex() {
    std::cout << "Testing invalid primary path index rejection..." << std::endl;
    SctpSocket sock("test-invalid-idx");
    
    sock.bind(0);
    
    std::vector<std::pair<std::string, uint16_t>> remotes = {
        {"127.0.0.1", 4234},
        {"127.0.0.1", 4235}
    };
    sock.connectMulti(remotes, 0);
    
    // Try to set invalid primary index
    bool ok = sock.setPrimaryPath(5);
    assert(!ok);  // Should reject out-of-range
    
    assert(sock.primaryRemoteIdx() == 0);  // Index should not change
    
    std::cout << "  ✓ Invalid primary index correctly rejected" << std::endl;
}

// Test connectMulti with invalid primary index
void testConnectWithInvalidPrimary() {
    std::cout << "Testing connectMulti with invalid primary index..." << std::endl;
    SctpSocket sock("test-connect-invalid-primary");
    
    sock.bind(0);
    
    std::vector<std::pair<std::string, uint16_t>> remotes = {
        {"127.0.0.1", 5234},
        {"127.0.0.1", 5235}
    };
    
    bool ok = sock.connectMulti(remotes, 10);  // Invalid index
    assert(!ok);  // Should reject
    
    std::cout << "  ✓ connectMulti correctly rejected invalid primary index" << std::endl;
}

// Test empty remote addresses rejection
void testEmptyRemoteAddresses() {
    std::cout << "Testing empty remote addresses rejection..." << std::endl;
    SctpSocket sock("test-empty-remote");
    
    sock.bind(0);
    
    std::vector<std::pair<std::string, uint16_t>> empty;
    bool ok = sock.connectMulti(empty, 0);
    assert(!ok);  // Should reject
    
    std::cout << "  ✓ Empty remote addresses correctly rejected" << std::endl;
}

int main() {
    std::cout << "=== SCTP Multi-homing Test Suite ===" << std::endl;
    std::cout << std::endl;
    
    // Initialize SCTP subsystem
    SctpSocket::wsaInit();
    
    try {
        testSingleAddressBind();
        std::cout << std::endl;
        
        testMultiAddressBind();
        std::cout << std::endl;
        
        testEmptyAddressList();
        std::cout << std::endl;
        
        testSingleAddressConnect();
        std::cout << std::endl;
        
        testMultiAddressConnect();
        std::cout << std::endl;
        
        testPrimaryPathSwitch();
        std::cout << std::endl;
        
        testInvalidPathIndex();
        std::cout << std::endl;
        
        testConnectWithInvalidPrimary();
        std::cout << std::endl;
        
        testEmptyRemoteAddresses();
        std::cout << std::endl;
        
        std::cout << "=== All tests PASSED ===" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
}
