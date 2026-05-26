//===-- PerfHelper.cpp ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "PerfHelper.h"
#include "Error.h"
#include "llvm/Config/config.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"
#ifdef HAVE_LIBPFM
#include <perfmon/perf_event.h>
#include <perfmon/pfmlib.h>
#include <perfmon/pfmlib_perf_event.h>
#endif

#include <cassert>
#include <cstddef>
#include <errno.h>  // for erno
#include <string.h> // for strerror()

namespace llvm {
namespace exegesis {
namespace pfm {

#ifdef HAVE_LIBPFM
static bool isPfmError(int Code) { return Code != PFM_SUCCESS; }
#endif

bool pfmInitialize() {
#ifdef HAVE_LIBPFM
  return isPfmError(pfm_initialize());
#else
  return true;
#endif
}

void pfmTerminate() {
#ifdef HAVE_LIBPFM
  pfm_terminate();
#endif
}

// Performance counters may be unavailable for a number of reasons (such as
// kernel.perf_event_paranoid restriction or CPU being unknown to libpfm).
//
// Dummy event can be specified to skip interaction with real performance
// counters while still passing control to the generated code snippet.
const char *const PerfEvent::DummyEventString = "not-really-an-event";

PerfEvent::~PerfEvent() {
#ifdef HAVE_LIBPFM
  delete Attr;
  ;
#endif
}

PerfEvent::PerfEvent(PerfEvent &&Other)
    : EventString(std::move(Other.EventString)),
      FullQualifiedEventString(std::move(Other.FullQualifiedEventString)),
      Attr(Other.Attr) {
  Other.Attr = nullptr;
}

PerfEvent::PerfEvent(StringRef PfmEventString)
    : EventString(PfmEventString.str()), Attr(nullptr) {
  if (PfmEventString != DummyEventString)
    initRealEvent(PfmEventString);
  else
    FullQualifiedEventString = PfmEventString;
}

void PerfEvent::initRealEvent(StringRef PfmEventString) {
#ifdef HAVE_LIBPFM
  char *Fstr = nullptr;
  pfm_perf_encode_arg_t Arg = {};
  Attr = new perf_event_attr();
  Arg.attr = Attr;
  Arg.fstr = &Fstr;
  Arg.size = sizeof(pfm_perf_encode_arg_t);
  const int Result = pfm_get_os_event_encoding(EventString.c_str(), PFM_PLM3,
                                               PFM_OS_PERF_EVENT, &Arg);
  if (isPfmError(Result)) {
    // We don't know beforehand which counters are available (e.g. 6 uops ports
    // on Sandybridge but 8 on Haswell) so we report the missing counter without
    // crashing.
    errs() << pfm_strerror(Result) << " - cannot create event " << EventString
           << "\n";
  }
  if (Fstr) {
    FullQualifiedEventString = Fstr;
    free(Fstr);
  }
#endif
}

// Accepts rNNNN hex form or field form (event=0x2e,umask=0x41[,...]).
Expected<RawCounter> parseRawCounter(StringRef Spec) {
  if (Spec.starts_with_insensitive("r")) {
    uint64_t Config;
    if (Spec.drop_front(1).getAsInteger(16, Config))
      return make_error<StringError>("invalid raw counter spec",
                                     errc::invalid_argument);
    return RawCounter{PERF_TYPE_RAW, Config, 0, 0};
  }

  // Field form: event=0x2e,umask=0x41[,cmask=0x1,inv=1,edge=1]
  uint64_t Event = 0, Umask = 0, Cmask = 0, Inv = 0, Edge = 0;
  bool FoundEvent = false;
  SmallVector<StringRef, 8> Fields;
  Spec.split(Fields, ',');

  for (StringRef Field : Fields) {
    auto [Key, Value] = Field.split('=');
    Key = Key.trim();
    Value = Value.trim();

    if (Key == "event") {
      if (Value.getAsInteger(0, Event) || Event > 0xFF)
        return make_error<StringError>(
            "invalid event value (must be 0x00-0xFF)", errc::invalid_argument);
      FoundEvent = true;
    } else if (Key == "umask") {
      if (Value.getAsInteger(0, Umask) || Umask > 0xFF)
        return make_error<StringError>(
            "invalid umask value (must be 0x00-0xFF)", errc::invalid_argument);
    } else if (Key == "cmask") {
      if (Value.getAsInteger(0, Cmask) || Cmask > 0xFF)
        return make_error<StringError>(
            "invalid cmask value (must be 0x00-0xFF)", errc::invalid_argument);
    } else if (Key == "inv") {
      if (Value.getAsInteger(0, Inv) || Inv > 1)
        return make_error<StringError>("invalid inv value (must be 0 or 1)",
                                       errc::invalid_argument);
    } else if (Key == "edge") {
      if (Value.getAsInteger(0, Edge) || Edge > 1)
        return make_error<StringError>("invalid edge value (must be 0 or 1)",
                                       errc::invalid_argument);
    } else {
      return make_error<StringError>("unknown field: " + Key.str(),
                                     errc::invalid_argument);
    }
  }

  if (!FoundEvent)
    return make_error<StringError>("missing required field: event",
                                   errc::invalid_argument);

  // Assemble PERFEVTSEL config per Intel SDM layout.
  uint64_t Config =
      Event | (Umask << 8) | (Edge << 18) | (Inv << 23) | (Cmask << 24);
  return RawCounter{PERF_TYPE_RAW, Config, 0, 0};
}

PerfEvent PerfEvent::fromRawConfig(uint32_t Type, uint64_t Config,
                                   uint64_t Config1, uint64_t Config2,
                                   StringRef Origin) {
  PerfEvent E;
  E.EventString = Origin.str();
  E.FullQualifiedEventString = Origin.str();
  E.initRawEvent(Type, Config, Config1, Config2);
  return E;
}

void PerfEvent::initRawEvent(uint32_t Type, uint64_t Config, uint64_t Config1,
                             uint64_t Config2) {
#ifdef HAVE_LIBPFM
  Attr = new perf_event_attr();
  Attr->size = sizeof(*Attr);
  Attr->type = Type;
  Attr->config = Config;
  Attr->config1 = Config1;
  Attr->config2 = Config2;
  Attr->read_format =
      PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING;
  Attr->exclude_kernel = 1;
  Attr->exclude_hv = 1;
}
#else
void PerfEvent::initRawEvent(uint32_t, uint64_t, uint64_t, uint64_t) {
  // No libpfm: Attr remains null. Will fall back to dummy counters
  // since perf_event_open is unavailable without libpfm support.
}
#endif

StringRef PerfEvent::name() const { return EventString; }

bool PerfEvent::valid() const { return !FullQualifiedEventString.empty(); }

const perf_event_attr *PerfEvent::attribute() const { return Attr; }

StringRef PerfEvent::getPfmEventString() const {
  return FullQualifiedEventString;
}

ConfiguredEvent::ConfiguredEvent(PerfEvent &&EventToConfigure)
    : Event(std::move(EventToConfigure)) {
  assert(Event.valid());
}

#ifdef HAVE_LIBPFM
void ConfiguredEvent::initRealEvent(const pid_t ProcessID, const int GroupFD) {
  const int CPU = -1;
  const uint32_t Flags = 0;
  perf_event_attr AttrCopy = *Event.attribute();
  AttrCopy.read_format =
      PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING;
  FileDescriptor = perf_event_open(&AttrCopy, ProcessID, CPU, GroupFD, Flags);
  if (FileDescriptor == -1) {
    errs() << "Unable to open event. ERRNO: " << strerror(errno)
           << ". Make sure your kernel allows user "
              "space perf monitoring.\nYou may want to try:\n$ sudo sh "
              "-c 'echo -1 > /proc/sys/kernel/perf_event_paranoid'.\n"
           << "If you are debugging and just want to execute the snippet "
              "without actually reading performance counters, "
              "pass --use-dummy-perf-counters command line option.\n";
  }
  assert(FileDescriptor != -1 && "Unable to open event");
}

Expected<SmallVector<int64_t>>
ConfiguredEvent::readOrError(StringRef /*unused*/) const {
  int64_t EventInfo[3] = {0, 0, 0};
  ssize_t ReadSize = ::read(FileDescriptor, &EventInfo, sizeof(EventInfo));

  if (ReadSize != sizeof(EventInfo))
    return make_error<StringError>("Failed to read event counter",
                                   errc::io_error);

  int64_t EventTimeEnabled = EventInfo[1];
  int64_t EventTimeRunning = EventInfo[2];
  if (EventTimeEnabled != EventTimeRunning)
    return make_error<PerfCounterNotFullyEnabled>();

  SmallVector<int64_t, 1> Result;
  Result.push_back(EventInfo[0]);
  return Result;
}

ConfiguredEvent::~ConfiguredEvent() { close(FileDescriptor); }
#else
void ConfiguredEvent::initRealEvent(pid_t ProcessID, const int GroupFD) {}

Expected<SmallVector<int64_t>>
ConfiguredEvent::readOrError(StringRef /*unused*/) const {
  return make_error<StringError>("Not implemented",
                                 errc::function_not_supported);
}

ConfiguredEvent::~ConfiguredEvent() = default;
#endif // HAVE_LIBPFM

CounterGroup::CounterGroup(PerfEvent &&E, std::vector<PerfEvent> &&ValEvents,
                           pid_t ProcessID)
    : EventCounter(std::move(E)) {
  IsDummyEvent = EventCounter.isDummyEvent();

  for (auto &&ValEvent : ValEvents)
    ValidationEventCounters.emplace_back(std::move(ValEvent));

  if (!IsDummyEvent)
    initRealEvent(ProcessID);
}

#ifdef HAVE_LIBPFM
void CounterGroup::initRealEvent(pid_t ProcessID) {
  EventCounter.initRealEvent(ProcessID);

  for (auto &ValCounter : ValidationEventCounters)
    ValCounter.initRealEvent(ProcessID, getFileDescriptor());
}

void CounterGroup::start() {
  if (!IsDummyEvent)
    ioctl(getFileDescriptor(), PERF_EVENT_IOC_RESET, PERF_IOC_FLAG_GROUP);
}

void CounterGroup::stop() {
  if (!IsDummyEvent)
    ioctl(getFileDescriptor(), PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP);
}

Expected<SmallVector<int64_t, 4>>
CounterGroup::readOrError(StringRef FunctionBytes) const {
  if (!IsDummyEvent)
    return EventCounter.readOrError(FunctionBytes);
  else
    return SmallVector<int64_t, 1>(1, 42);
}

Expected<SmallVector<int64_t>>
CounterGroup::readValidationCountersOrError() const {
  SmallVector<int64_t, 4> Result;
  for (const auto &ValCounter : ValidationEventCounters) {
    Expected<SmallVector<int64_t>> ValueOrError =
        ValCounter.readOrError(StringRef());

    if (!ValueOrError)
      return ValueOrError.takeError();

    // Reading a validation counter will only return a single value, so it is
    // safe to only append the first value here. Also assert that this is true.
    assert(ValueOrError->size() == 1 &&
           "Validation counters should only return a single value");
    Result.push_back((*ValueOrError)[0]);
  }
  return Result;
}

int CounterGroup::numValues() const { return 1; }
#else

void CounterGroup::initRealEvent(pid_t ProcessID) {}

void CounterGroup::start() {}

void CounterGroup::stop() {}

Expected<SmallVector<int64_t, 4>>
CounterGroup::readOrError(StringRef /*unused*/) const {
  if (IsDummyEvent) {
    SmallVector<int64_t, 4> Result;
    Result.push_back(42);
    return Result;
  }
  return make_error<StringError>("Not implemented", errc::io_error);
}

Expected<SmallVector<int64_t>>
CounterGroup::readValidationCountersOrError() const {
  return SmallVector<int64_t>(0);
}

int CounterGroup::numValues() const { return 1; }

#endif

} // namespace pfm
} // namespace exegesis
} // namespace llvm
