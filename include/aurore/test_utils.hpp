#ifndef AURORE_TEST_UTILS_HPP
#define AURORE_TEST_UTILS_HPP

#include "test_infrastructure.hpp"

namespace aurore {
namespace test {

// Using declarations for convenience
using StackTracker = aurore::test::StackTracker;
using HeapTracker = aurore::test::HeapTracker;
using DmaHealthMonitor = aurore::test::DmaHealthMonitor;
using ThermalHealthMonitor = aurore::test::ThermalHealthMonitor;
using ResourceMonitor = aurore::test::ResourceMonitor;
using QueueStressTest = aurore::test::QueueStressTest;
using ConcurrencyPathologyDetector = aurore::test::ConcurrencyPathologyDetector;
using TimestampValidator = aurore::test::TimestampValidator;
using NumericRobustnessTester = aurore::test::NumericRobustnessTester;
using HostileInputInjector = aurore::test::HostileInputInjector;
using ResetScenarioTester = aurore::test::ResetScenarioTester;
using ConfigReloadTester = aurore::test::ConfigReloadTester;
using LogTester = aurore::test::LogTester;
using OffsetTracker = aurore::test::OffsetTracker;
using FaultTimeline = aurore::test::FaultTimeline;
using CrashDumpTester = aurore::test::CrashDumpTester;
using FaultInjector = aurore::test::FaultInjector;
using TestResultAggregator = aurore::test::TestResultAggregator;

// Enums for convenience
using FaultTarget = aurore::test::FaultTarget;
using FaultType = aurore::test::FaultType;
using BackpressurePolicy = aurore::test::BackpressurePolicy;
using ResetType = aurore::test::ResetType;
using TestTier = aurore::test::TestTier;

} // namespace test
} // namespace aurore

#endif // AURORE_TEST_UTILS_HPP