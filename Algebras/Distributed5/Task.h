/*
----
This file is part of SECONDO.

Copyright (C) 2015,
Faculty of Mathematics and Computer Science,
Database Systems for New Applications.

SECONDO is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

SECONDO is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with SECONDO; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
----


//[$][\$]

*/

#ifndef DISTRIBUTE5_TASK_H
#define DISTRIBUTE5_TASK_H
#include "Attribute.h"
#include "Algebra.h"
#include "NestedList.h"
#include "QueryProcessor.h"
#include "AlgebraManager.h"
#include "Operator.h"
#include "StandardTypes.h"
#include "Symbols.h"
#include "ListUtils.h"
#include "Algebras/Distributed2/CommandLogger.h"
#include "Algebras/Array/ArrayAlgebra.h"
#include "SocketIO.h"
#include "Algebras/Distributed2/FileRelations.h"
#include "Algebras/Distributed2/fsrel.h"
#include "Stream.h"
#include "Algebras/Distributed2/DArray.h"
#include "Algebras/Distributed2/Distributed2Algebra.h"
#include "Algebras/Distributed2/ConnectionInfo.h"
#include "Algebras/Distributed2/DFSType.h"
#include <chrono>

#include <boost/bind.hpp>
#include <boost/ref.hpp>
#include <boost/thread.hpp>

#include <set>
#include <optional>

// #define TASK_VERIFY_COUNTS

namespace distributed5
{
enum DataStorageType
{
    Object,
    File
};

enum WorkerDistance
{
    SameProcess,
    SameServer,
    OtherServer
};

enum DataDistance : int
{
    MemoryOnWorker = 10,
    FileOnServer = 20,
    AccessibleWithCorrectType = 21,
    MemoryOnWorkerButNeedFile = 500,
    Accessible = 501,
    FileOverNetwork = 1000,
    MemoryOnServer = 2000,
    MemoryOverNetwork = 10000,
    FarAway = 10001
};

#define CostNotPreferredServer 2000
#define CostNotPreferredWorker 100
#define CostMissingArgument 10000
#define CostReservation 1500

#define CostConvertToFile 0
#define CostConvertToObject 100
#define CostTransfer 1000
#define CostActiveTransfers 1
#define CostWaitingOnTransfer 10000

class TaskDataItem;

class WorkerLocation
{
public:
    WorkerLocation(
        std::string server,
        int port,
        std::string config,
        int worker)
        : server(server), port(port), config(config), worker(worker) {}

    const std::string &getServer() const { return server; }
    const std::string &getConfig() const { return config; }
    int getPort() const { return port; }
    int getWorker() const { return worker; }
    distributed2::DArrayElement getDArrayElement() const;

    std::string toString() const
    {
        return server +
               ":" + std::to_string(port) +
               " (" + std::to_string(worker) + " " + config + ")";
    }

    distributed2::ConnectionInfo *getWorkerConnection() const;

    std::string getFileBase(const TaskDataItem *data) const;

    std::string getFilePath(const TaskDataItem *data) const;

    std::string getFileDirectory(const TaskDataItem *data) const;

    bool operator==(WorkerLocation const &other) const
    {
        return server == other.server &&
               port == other.port &&
               config == other.config &&
               worker == other.worker;
    }

    bool operator!=(WorkerLocation const &other) const
    {
        return !(*this == other);
    }

    bool operator<(WorkerLocation const &other) const
    {
        return worker < other.worker;
    }

    bool operator>(WorkerLocation const &other) const
    {
        return worker > other.worker;
    }

    WorkerDistance getDistance(WorkerLocation const &other) const
    {
        if (server == other.server)
        {
            if (*this == other)
            {
                return WorkerDistance::SameProcess;
            }
            return WorkerDistance::SameServer;
        }
        return WorkerDistance::OtherServer;
    }

private:
    std::string server;
    int port;
    std::string config;
    int worker;
};

class TaskDataLocation
{
public:
    TaskDataLocation(
        std::string server,
        int port,
        std::string config,
        int worker,
        DataStorageType storageType,
        bool temporary)
        : workerLocation(server, port, config, worker),
          storageType(storageType), temporary(temporary) {}

    TaskDataLocation(
        WorkerLocation workerLocation,
        DataStorageType storageType,
        bool temporary)
        : workerLocation(workerLocation),
          storageType(storageType), temporary(temporary) {}

    DataStorageType getStorageType() const { return storageType; }
    bool isTemporary() const { return temporary; }

    const WorkerLocation &getWorkerLocation() const { return workerLocation; }
    const std::string &getServer() const { return workerLocation.getServer(); }
    const std::string &getConfig() const { return workerLocation.getConfig(); }
    int getPort() const { return workerLocation.getPort(); }
    int getWorker() const { return workerLocation.getWorker(); }

    distributed2::DArrayElement getDArrayElement() const
    {
        return workerLocation.getDArrayElement();
    }

    std::string toString() const
    {
        return std::string(storageType == Object ? "object " : "file ") +
               (temporary ? "T " : "P ") +
               workerLocation.toString();
    }

    distributed2::ConnectionInfo *getWorkerConnection() const
    {
        return workerLocation.getWorkerConnection();
    }

    std::string getFileBase(const TaskDataItem *data) const
    {
        return workerLocation.getFileBase(data);
    }

    std::string getFilePath(const TaskDataItem *data) const
    {
        return workerLocation.getFilePath(data);
    }

    std::string getFileDirectory(const TaskDataItem *data) const
    {
        return workerLocation.getFileDirectory(data);
    }

    std::string getValueArgument(const TaskDataItem *data) const;

    DataDistance getDistance(WorkerLocation const &loc,
                             bool needFile) const
    {
        WorkerDistance dist = workerLocation.getDistance(loc);
        switch (storageType)
        {
        case Object:
            switch (dist)
            {
            case SameProcess:
                if (needFile)
                    return DataDistance::MemoryOnWorkerButNeedFile;
                return DataDistance::MemoryOnWorker;
            case SameServer:
                return DataDistance::MemoryOnServer;
            case OtherServer:
                return DataDistance::MemoryOverNetwork;
            }
            break;
        case File:
            switch (dist)
            {
            case SameProcess:
                return DataDistance::FileOnServer;
            case SameServer:
                return DataDistance::FileOnServer;
            case OtherServer:
                return DataDistance::FileOverNetwork;
            }
            break;
        }
        // This does not happen
        return (DataDistance)-1;
    }

    bool operator==(TaskDataLocation const &other) const
    {
        return workerLocation == other.workerLocation &&
               storageType == other.storageType &&
               temporary == other.temporary;
    }

    bool operator!=(TaskDataLocation const &other) const
    {
        return !(*this == other);
    }

#ifdef TASK_VERIFY_COUNTS
    size_t getValueCount(const TaskDataItem *data) const;
    std::string getValue(const TaskDataItem *data) const;
#endif

private:
    WorkerLocation workerLocation;
    DataStorageType storageType;
    bool temporary;
};

class TaskDataItem
{
public:
    TaskDataItem(std::string name, size_t slot, ListExpr contentType,
                 TaskDataLocation location)
        : TaskDataItem(name, slot, 0, contentType,
                       location) {}

    TaskDataItem(std::string name,
                 size_t slot, size_t vslot,
                 ListExpr contentType,
                 TaskDataLocation location)
        : name(name), slot(slot), vslot(vslot), contentType(contentType)
    {
        auto &locations = locationsByServer[location.getServer()];
        locations.push_back(location);
        objectLocations = location.getStorageType() == Object ? 1 : 0;
        fileLocations = location.getStorageType() == File ? 1 : 0;
        objectRelation = Relation::checkType(contentType);
        fileRelation = distributed2::frel::checkType(contentType);
    }

    TaskDataItem(const distributed5::TaskDataItem &copy)
        : locationsByServer(copy.locationsByServer),
          objectLocations(copy.objectLocations),
          fileLocations(copy.fileLocations),
          name(copy.name),
          slot(copy.slot), vslot(copy.vslot),
          contentType(copy.contentType),
          fileRelation(copy.fileRelation),
          objectRelation(copy.objectRelation) {}

    std::string getName() const { return name; }
    size_t getSlot() const { return slot; }
    size_t getVerticalSlot() const { return vslot; }
    ListExpr getContentType() const { return contentType; }

    bool isFileRelation() const { return fileRelation; }
    bool isObjectRelation() const { return objectRelation; }

#ifdef TASK_VERIFY_COUNTS
    size_t getCount() const
    {
        boost::shared_lock_guard<boost::shared_mutex> lock(mutex);
        return count;
    }

    void setCount(size_t currentCount)
    {
        boost::lock_guard<boost::shared_mutex> lock(mutex);
        count = currentCount;
    }

    static void verifyCount(size_t currentCount, size_t expectedCount)
    {
        if (expectedCount != currentCount)
        {
            std::cout << "Got count " << currentCount
                      << ", but expected " << expectedCount << endl;
        }
    }

    void verifyCount(size_t currentCount) const
    {
        boost::shared_lock_guard<boost::shared_mutex> lock(mutex);
        verifyCount(currentCount, count);
    }

    void verifyCount(std::string currentCount, size_t offset = 0) const
    {
        size_t value = parseCount(currentCount) + offset;
        boost::shared_lock_guard<boost::shared_mutex> lock(mutex);
        verifyCount(value, count);
    }

    static size_t parseCount(std::string count)
    {
        if (count.substr(0, 5) == "(int " &&
            count.substr(count.length() - 1, 1) == ")")
        {
            return std::stoi(count.substr(5, count.length() - 6));
        }
        return 0;
    }
#endif

    std::string toString() const
    {
        boost::shared_lock_guard<boost::shared_mutex> lock(mutex);
        std::string slotInfo =
            vslot != 0
                ? " _ " + std::to_string(slot) +
                      " _ " + std::to_string(vslot - 1)
                : " _ " + std::to_string(slot);
        std::string str = name + slotInfo;
        for (auto &locations : locationsByServer)
        {
            for (auto &location : locations.second)
            {
                str += " @[" + location.toString() + "]";
            }
        }
        return str;
    }

    std::string getObjectName() const
    {
        if (vslot != 0)
        {
            return name + "_" + std::to_string(slot) +
                   "_" + std::to_string(vslot - 1);
        }
        return name + "_" + std::to_string(slot);
    }

    bool hasLocation(TaskDataLocation const &location) const;

    TaskDataLocation findLocation(WorkerLocation const &nearby) const;
    bool hasLocation(WorkerLocation const &nearby) const;
    TaskDataLocation findUpcomingLocation(WorkerLocation const &nearby) const;
    bool hasUpcomingLocation(WorkerLocation const &nearby) const;
    bool hasUpcomingLocation(WorkerLocation const &nearby,
                             DataStorageType storageType) const;
    bool hasLocation(WorkerLocation const &nearby,
                     DataStorageType storageType) const;
    TaskDataLocation findLocation(WorkerLocation const &nearby,
                                  DataStorageType storageType) const;
    std::pair<TaskDataLocation, int> findTransferSourceLocation(
        std::map<std::string, std::pair<bool, int>> activeTransferrators) const;
    TaskDataLocation getFirstLocation() const;
    std::vector<TaskDataLocation> getLocations() const;

    DataDistance getDistance(WorkerLocation const &location) const;
    DataDistance getUpcomingDistance(WorkerLocation const &location) const;

    std::string getValueArgument(WorkerLocation const &nearby) const
    {
        if (isObjectRelation())
        {
            auto loc = findLocation(nearby, DataStorageType::Object);
            return loc.getValueArgument(this);
        }
        else if (isFileRelation())
        {
            auto loc = findLocation(nearby, DataStorageType::File);
            return loc.getValueArgument(this);
        }
        else
        {
            auto loc = findLocation(nearby);
            return loc.getValueArgument(this);
        }
    }

    void merge(TaskDataItem *other);

    void removeLocation(TaskDataLocation location)
    {
        boost::lock_guard<boost::shared_mutex> lock(mutex);
        auto &locations = locationsByServer[location.getServer()];
        for (auto it = locations.begin(); it != locations.end(); it++)
        {
            if (*it == location)
            {
                if (location.getStorageType() == File)
                    fileLocations--;
                if (location.getStorageType() == Object)
                    objectLocations--;
                locations.erase(it);
                return;
            }
        }
    }

    void addLocation(TaskDataLocation location)
    {
        boost::lock_guard<boost::shared_mutex> lock(mutex);
        auto &locations = locationsByServer[location.getServer()];
        if (location.getStorageType() == File)
            fileLocations++;
        if (location.getStorageType() == Object)
            objectLocations++;
        locations.push_back(location);
        for (auto it = upcomingLocations.begin();
             it != upcomingLocations.end();
             it++)
        {
            if (*it == location)
            {
                upcomingLocations.erase(it);
                break;
            }
        }
    }

    void persistLocation(TaskDataLocation location)
    {
        boost::lock_guard<boost::shared_mutex> lock(mutex);
        auto &locations = locationsByServer[location.getServer()];
        for (auto it = locations.begin(); it != locations.end(); it++)
        {
            if (*it == location)
            {
                *it = TaskDataLocation(
                    location.getWorkerLocation(),
                    location.getStorageType(), false);
                return;
            }
        }
    }

    bool addUpcomingLocation(TaskDataLocation location);

private:
    mutable boost::shared_mutex mutex;
    std::map<std::string, std::vector<TaskDataLocation>>
        locationsByServer;
    int objectLocations;
    int fileLocations;
    std::list<TaskDataLocation> upcomingLocations;
    std::string name;
    size_t slot;
    size_t vslot;
    ListExpr contentType;
    bool fileRelation;
    bool objectRelation;

#ifdef TASK_VERIFY_COUNTS
    size_t count = 0;
#endif
};

enum TaskFlag : int
{
    None = 0x0,
    Output = 0x1,
    VerticalSlot = 0x2,

    CopyArguments = 0x4,
    ConvertArguments = 0x8,

    PrimaryArgumentAsFile = 0x10,
    SecondaryArgumentsAsFile = 0x20,
    PrimaryArgumentAsObject = 0x40,
    SecondaryArgumentsAsObject = 0x80,

    RunOnPreferedWorker = 0x100,
    RunOnPreferedServer = 0x200,
    RunOnReceive = 0x400,

    PreferSlotWorker = 0x1000,
    PreferSlotServer = 0x2000,
};

class TaskStatistics
{
public:
    class Entry
    {
    public:
        double value;
        double min = std::numeric_limits<double>::infinity();
        double max = -std::numeric_limits<double>::infinity();
        int count;
        std::list<double> values;
    };

    static void report(std::string name, double value)
    {
        local.addValue(name, value);
    }

    void addValue(std::string name, double value)
    {
        auto &entry = values[name];
        entry.value += value;
        if (value > entry.max)
            entry.max = value;
        if (value < entry.min)
            entry.min = value;
        entry.count++;
        if (entry.count < 10000)
            entry.values.push_back(value);
    }

    static TaskStatistics &getThreadLocal()
    {
        return local;
    }

    void merge(TaskStatistics other)
    {
        auto start = std::chrono::high_resolution_clock::now();
        for (auto pair : other.values)
        {
            auto &entry = values[pair.first];
            entry.value += pair.second.value;
            if (pair.second.max > entry.max)
                entry.max = pair.second.max;
            if (pair.second.min < entry.min)
                entry.min = pair.second.min;
            entry.count += pair.second.count;
            if (entry.count < 10000)
            {
                for (double value : pair.second.values)
                    entry.values.push_back(value);
            }
        }
        auto duration =
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::high_resolution_clock::now() - start);
        addValue("merge stats",
                 ((double)duration.count()) / 1000000);
    }

    std::string toString()
    {
        std::string buf;
        for (auto pair : values)
        {
            int count = pair.second.count;
            bool few = count < 10000;
            double mean = pair.second.value / count;
            double variance = 0;
            if (few)
            {
                for (double value : pair.second.values)
                {
                    double d = value - mean;
                    variance += d * d;
                }
            }
            buf += pair.first +
                   ": total: " + std::to_string(pair.second.value) +
                   ", count: " + std::to_string(pair.second.count) +
                   ", min: " + std::to_string(pair.second.min) +
                   ", max: " + std::to_string(pair.second.max) +
                   ", mean: " + std::to_string(mean);
            if (few)
            {
                buf += ", stdev: " + std::to_string(sqrt(variance));
            }
            buf += "\n";
        }
        return buf;
    }

private:
    std::map<std::string, Entry> values;
    static thread_local TaskStatistics local;
};

class Task
{
public:
    Task(WorkerLocation preferredLocation, int flags = 0)
        : preferredLocation(preferredLocation), flags(flags), id(nextId++) {}
    virtual ~Task();

    int getId();

    bool hasFlag(TaskFlag flag) { return bool(flags & flag); }
    void setFlag(TaskFlag flag) { flags = flags | flag; }
    void clearFlag(TaskFlag flag) { flags = flags & ~flag; }

    void addPredecessorTask(Task *t) { addArgument(t, 0); }
    void addArgument(Task *task, size_t pos);
    std::vector<Task *> getPredecessors();
    std::vector<std::pair<Task *, size_t>> &getArguments();

    virtual std::string toString() const
    {
        return getTaskType() + " task";
    };

    virtual size_t getNumberOfResults() const
    {
        return 1;
    }

    virtual std::string getTaskType() const = 0;
    virtual std::vector<TaskDataItem *> run(
        WorkerLocation &location,
        std::vector<TaskDataItem *> args) = 0;

    const WorkerLocation &getPreferredLocation() const
    {
        return preferredLocation;
    }

    static const std::string BasicType();
    static const bool checkType(const ListExpr list);
    static const ListExpr innerType(const ListExpr list)
    {
        return nl->Second(list);
    }
    static const ListExpr resultType(const ListExpr list)
    {
        ListExpr arrayType = nl->Second(list);
        if (distributed2::DArray::checkType(arrayType))
        {
            return nl->Second(arrayType);
        }
        return nl->TwoElemList(
            listutils::basicSymbol<distributed2::frel>(),
            nl->Second(nl->Second(arrayType)));
    }

    static double runCommand(distributed2::ConnectionInfo *ci,
                             std::string cmd,
                             std::string description,
                             bool nestedListFormat = false,
                             std::string expectResult = "(bool TRUE)",
                             bool ignoreError = false,
                             std::string *result = 0);

private:
    std::vector<std::pair<Task *, size_t>> arguments;
    WorkerLocation preferredLocation;

    int flags;
    static int nextId;
    int id;
};

// Data stored on a Worker
// might by in object and/or file form
// depending on storageType
class DataTask : public Task
{
public:
    DataTask(const distributed2::DArrayElement dArrayElement,
             std::string name,
             size_t slot,
             DataStorageType storageType,
             ListExpr contentType);

    virtual std::string getTaskType() const { return "data"; }

    virtual std::vector<TaskDataItem *> run(
        WorkerLocation &location,
        std::vector<TaskDataItem *> args);

private:
    TaskDataItem dataItem;
};

class WorkerTask : public Task
{
public:
    WorkerTask(const WorkerLocation location)
        : Task(location, RunOnReceive),
          location(location) {}

    WorkerTask(const distributed2::DArrayElement dArrayElement);

    virtual std::string getTaskType() const { return "worker"; }

    virtual std::string toString() const
    {
        return "worker " + location.toString();
    }

    virtual std::vector<TaskDataItem *> run(
        WorkerLocation &location,
        std::vector<TaskDataItem *> args)
    {
        return std::vector{
            new TaskDataItem(std::string(""), 0, nl->TheEmptyList(),
                             TaskDataLocation(this->location, Object, false))};
    }

private:
    WorkerLocation location;
};

class FunctionTask : public Task
{
protected:
    FunctionTask(WorkerLocation preferredLocation,
                 int additonalFlags,
                 std::string resultName,
                 ListExpr resultContentType,
                 bool isRel,
                 bool isStream)
        : Task(preferredLocation, CopyArguments | ConvertArguments |
                                      PreferSlotWorker | PreferSlotServer |
                                      additonalFlags),
          resultName(resultName),
          resultContentType(resultContentType),
          isRel(isRel),
          isStream(isStream) {}

public:
    virtual std::string toString() const
    {
        return getTaskType() + "[" + resultName + "] => " +
               nl->ToString(resultContentType);
    }

protected:
    std::string resultName;
    ListExpr resultContentType;
    bool isRel;
    bool isStream;
    bool storeRelAsObject = false;

    std::vector<TaskDataItem *> store(
        const WorkerLocation &location,
        size_t slot, std::string value, std::string description,
        size_t expectedCount = 0);
};

class DmapFunctionTask : public FunctionTask
{
public:
    DmapFunctionTask(WorkerLocation preferredLocation,
                     std::string mapFunction,
                     std::string resultName,
                     ListExpr resultContentType,
                     bool isRel,
                     bool isStream)
        : FunctionTask(preferredLocation,
                       0,
                       resultName,
                       resultContentType,
                       isRel,
                       isStream),
          mapFunction(mapFunction)
    {
        storeRelAsObject = true;
    }

    virtual std::string getTaskType() const { return "dmap"; }

    virtual std::string toString() const
    {
        return getTaskType() + "[" + resultName + ", " + mapFunction + "] => " +
               nl->ToString(resultContentType);
    }

    virtual std::vector<TaskDataItem *> run(
        WorkerLocation &location,
        std::vector<TaskDataItem *> args);

protected:
    std::string mapFunction;
};

class DproductFunctionTask : public FunctionTask
{
public:
    DproductFunctionTask(WorkerLocation preferredLocation,
                         std::string mapFunction,
                         std::string resultName,
                         ListExpr resultContentType,
                         bool isRel,
                         bool isStream)
        : FunctionTask(preferredLocation,
                       SecondaryArgumentsAsFile,
                       resultName,
                       resultContentType,
                       isRel,
                       isStream),
          mapFunction(mapFunction)
    {
    }

    virtual std::string getTaskType() const { return "dproduct"; }

    virtual std::string toString() const
    {
        return getTaskType() + "[" + resultName + ", " + mapFunction + "] => " +
               nl->ToString(resultContentType);
    }

    virtual std::vector<TaskDataItem *> run(
        WorkerLocation &location,
        std::vector<TaskDataItem *> args);

protected:
    std::string mapFunction;
};

class PartitionFunctionTask : public FunctionTask
{
public:
    PartitionFunctionTask(WorkerLocation preferredLocation,
                          std::string mapFunction,
                          std::string partitionFunction,
                          std::string resultName,
                          size_t vslots,
                          ListExpr resultContentType)
        : FunctionTask(preferredLocation,
                       PrimaryArgumentAsFile | SecondaryArgumentsAsFile,
                       resultName,
                       resultContentType,
                       false,
                       false),
          mapFunction(mapFunction),
          partitionFunction(partitionFunction),
          vslots(vslots)
    {
    }

    virtual std::string getTaskType() const { return "partition"; }

    virtual size_t getNumberOfResults() const { return vslots; }

    virtual std::vector<TaskDataItem *> run(
        WorkerLocation &location,
        std::vector<TaskDataItem *> args);

    virtual std::string toString() const
    {
        return getTaskType() + "[" + resultName + ", " + mapFunction + ", " +
               partitionFunction + ", " + std::to_string(vslots) +
               "] => " + nl->ToString(resultContentType);
    }

protected:
    std::string mapFunction;
    std::string partitionFunction;
    size_t vslots;
};

class CollectFunctionTask : public FunctionTask
{
public:
    CollectFunctionTask(WorkerLocation preferredLocation,
                        std::string resultName, ListExpr resultContentType)
        : FunctionTask(preferredLocation,
                       PrimaryArgumentAsFile | SecondaryArgumentsAsFile,
                       resultName, resultContentType, true, false) {}

    virtual std::string getTaskType() const { return "collect"; }

    virtual std::vector<TaskDataItem *> run(
        WorkerLocation &location,
        std::vector<TaskDataItem *> args);

    virtual std::string toString() const
    {
        return getTaskType() + "[" + resultName + "] => " +
               nl->ToString(resultContentType);
    }
};

class ErrorTask : public Task
{
public:
    ErrorTask() : Task(WorkerLocation("", 0, "", 0)) {}

    virtual std::string getTaskType() const { return "error"; }

    virtual std::vector<TaskDataItem *> run(
        WorkerLocation &location,
        std::vector<TaskDataItem *> args)
    {
        throw std::invalid_argument("Error task should not exist in stream");
    }
};

class RemoteException : public std::exception
{
public:
    RemoteException(std::string description, std::string error, std::string cmd)
        : description(description), error(error), cmd(cmd) {}
    const char *what() const throw()
    {
        if (message.empty())
        {
            message = description + " failed: " + error + "\n" +
                      "command = " + cmd;
        }
        return message.c_str();
    }

private:
    std::string description;
    std::string error;
    std::string cmd;
    mutable std::string message;
};

class NoNearbyLocationException : public std::exception
{
public:
    NoNearbyLocationException(const TaskDataItem *data, WorkerLocation nearby)
        : data(data), nearby(nearby) {}
    const char *what() const throw()
    {
        if (message.empty())
        {
            message = "TaskDataItem (" + data->toString() + ")" +
                      " is not stored nearby " + nearby.toString();
        }
        return message.c_str();
    }

private:
    const TaskDataItem *data;
    WorkerLocation nearby;
    mutable std::string message;
};

class NoSourceLocationException : public std::exception
{
public:
    NoSourceLocationException(const TaskDataItem *data)
        : data(data) {}
    const char *what() const throw()
    {
        if (message.empty())
        {
            message = "TaskDataItem (" + data->toString() + ")" +
                      " is not transferable";
        }
        return message.c_str();
    }

private:
    const TaskDataItem *data;
    mutable std::string message;
};

extern TypeConstructor TaskTC;
} // namespace distributed5

#endif
