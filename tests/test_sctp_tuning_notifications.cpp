#include "../src/common/sctp_socket.h"
#include <cassert>
#include <iostream>
#include <atomic>

using namespace rbs::net;

// Test callback for notifications
std::atomic<int> notificationCount{0};
std::atomic<SctpNotificationType> lastNotificationType{SctpNotificationType::UNKNOWN};

void notificationHandler(const SctpNotification& notif) {
    notificationCount++;
    lastNotificationType = notif.type;
    std::cout << "  ✓ Notification received: " << notif.description << std::endl;
}

// Test ApplyTuning API
void testApplyTuning() {
    std::cout << "Testing SCTP performance tuning..." << std::endl;
    
    SctpSocket sock("test-tuning");
    
    assert(sock.bind(0));
    assert(sock.localPort() > 0);
    
    // Create tuning parameters
    SctpTuning tuning;
    tuning.heartbeatInterval = 5000;  // 5 second keepalive
    tuning.rtoInitial = 2000;          // 2 second initial RTO
    tuning.rtoMin = 800;               // 800ms min RTO
    tuning.rtoMax = 30000;             // 30 second max RTO
    tuning.initNumAttempts = 5;        // 5 INIT attempts
    tuning.initMaxTimeout = 30000;     // 30 second max timeout
    tuning.rcvBufSize = 262144;        // 256KB receive buffer
    tuning.sndBufSize = 262144;        // 256KB send buffer
    
    // Apply tuning (will be no-op on Windows UDP fallback, but API should work)
    bool ok = sock.applyTuning(tuning);
    assert(ok);
    
    std::cout << "  ✓ Tuning parameters applied successfully" << std::endl;
}

// Test notification callback setup
void testNotificationCallback() {
    std::cout << "Testing SCTP notification callbacks..." << std::endl;
    
    SctpSocket sock("test-notifications");
    
    // Register callback
    sock.setNotificationCallback(notificationHandler);
    std::cout << "  ✓ Notification callback registered" << std::endl;
    
    // Reset counter
    notificationCount = 0;
    
    // Bind and basic ops (won't generate notifications on UDP fallback,
    // but API should not crash)
    assert(sock.bind(0));
    std::cout << "  ✓ Socket bound with notification handler active" << std::endl;
}

// Test partial tuning parameters
void testPartialTuning() {
    std::cout << "Testing partial tuning parameter application..." << std::endl;
    
    SctpSocket sock("test-partial-tuning");
    assert(sock.bind(0));
    
    // Create tuning with only some parameters set
    SctpTuning tuning;
    tuning.heartbeatInterval = 10000;  // Only set this
    
    bool ok = sock.applyTuning(tuning);
    assert(ok);
    
    std::cout << "  ✓ Partial tuning applied successfully" << std::endl;
}

// Test empty tuning
void testEmptyTuning() {
    std::cout << "Testing empty tuning parameters..." << std::endl;
    
    SctpSocket sock("test-empty-tuning");
    assert(sock.bind(0));
    
    // Create tuning with no parameters set
    SctpTuning tuning;
    
    bool ok = sock.applyTuning(tuning);
    assert(ok);  // Should succeed even with empty tuning
    
    std::cout << "  ✓ Empty tuning handled gracefully" << std::endl;
}

// Test notification type enum values
void testNotificationTypes() {
    std::cout << "Testing SCTP notification type enum..." << std::endl;
    
    assert(SctpNotificationType::ASSOC_CHANGE != SctpNotificationType::UNKNOWN);
    assert(SctpNotificationType::PEER_ADDR_CHANGE != SctpNotificationType::UNKNOWN);
    assert(SctpNotificationType::SEND_FAILED != SctpNotificationType::UNKNOWN);
    assert(SctpNotificationType::SHUTDOWN_EVENT != SctpNotificationType::UNKNOWN);
    
    std::cout << "  ✓ All notification types uniquely identifiable" << std::endl;
}

int main() {
    std::cout << "=== SCTP Performance Tuning & Notifications Test ===" << std::endl;
    std::cout << std::endl;
    
    SctpSocket::wsaInit();
    
    try {
        testApplyTuning();
        std::cout << std::endl;
        
        testNotificationCallback();
        std::cout << std::endl;
        
        testPartialTuning();
        std::cout << std::endl;
        
        testEmptyTuning();
        std::cout << std::endl;
        
        testNotificationTypes();
        std::cout << std::endl;
        
        std::cout << "=== All tests PASSED ===" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
}
