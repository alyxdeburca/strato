#pragma once

#include "KThread.h"
#include "KPrivateMemory.h"
#include "KTransferMemory.h"
#include "KSharedMemory.h"
#include "KSession.h"
#include "KEvent.h"
#include <kernel/memory.h>
#include <condition_variable>

namespace skyline::kernel::type {
    /**
     * @brief The KProcess class is responsible for holding the state of a process
     */
    class KProcess : public KSyncObject {
      private:
        /**
         * @brief This class holds a single TLS page's status
         * @details tls_page_t holds the status of a single TLS page (A page is 4096 bytes on ARMv8).
         * Each TLS page has 8 slots, each 0x200 (512) bytes in size.
         * The first slot of the first page is reserved for user-mode exception handling.
         * Read more about TLS here: https://switchbrew.org/wiki/Thread_Local_Storage
         */
        struct TlsPage {
            u64 address; //!< The address of the page allocated for TLS
            u8 index = 0; //!< The slots are assigned sequentially, this holds the index of the last TLS slot reserved
            bool slot[constant::TlsSlots]{0}; //!< An array of booleans denoting which TLS slots are reserved

            /**
             * @param address The address of the allocated page
             */
            TlsPage(u64 address);

            /**
             * @brief Reserves a single 0x200 byte TLS slot
             * @return The address of the reserved slot
             */
            u64 ReserveSlot();

            /**
             * @brief Returns the address of a particular slot
             * @param slotNo The number of the slot to be returned
             * @return The address of the specified slot
             */
            u64 Get(u8 slotNo);

            /**
             * @brief Returns boolean on if the TLS page has free slots or not
             * @return If the whole page is full or not
             */
            bool Full();
        };

        /**
         * @brief Returns a TLS slot from an arbitrary TLS page
         * @return The address of a free TLS slot
         */
        u64 GetTlsSlot();

        /**
         * @brief This initializes heap and the initial TLS page
         */
        void InitializeMemory();

      public:
        friend OS;

        /**
         * @brief This is used as the output for functions that return created kernel objects
         * @tparam objectClass The class of the kernel object
         */
        template<typename objectClass>
        struct HandleOut {
            std::shared_ptr<objectClass> item; //!< A shared pointer to the object
            handle_t handle; //!< The handle of the object in the process
        };

        /**
         * @brief This enum is used to describe the current status of the process
         */
        enum class Status {
            Created, //!< The process was created but the main thread has not started yet
            Started, //!< The process has been started
            Exiting //!< The process is exiting
        } status = Status::Created; //!< The state of the process

        /**
         * @brief This is used to hold information about a single waiting thread for mutexes and conditional variables
         */
        struct WaitStatus {
            std::atomic_bool flag{false}; //!< The underlying atomic flag of the thread
            u8 priority; //!< The priority of the thread
            pid_t pid; //!< The PID of the thread

            WaitStatus(u8 priority, pid_t pid) : priority(priority), pid(pid) {}
        };

        handle_t handleIndex = constant::BaseHandleIndex; //!< This is used to keep track of what to map as an handle
        pid_t pid; //!< The PID of the main thread
        int memFd; //!< The file descriptor to the memory of the process
        std::unordered_map<handle_t, std::shared_ptr<KObject>> handles; //!< A mapping from a handle_t to it's corresponding KObject which is the actual underlying object
        std::unordered_map<pid_t, std::shared_ptr<KThread>> threads; //!< A mapping from a PID to it's corresponding KThread object
        std::unordered_map<u64, std::vector<std::shared_ptr<WaitStatus>>> mutexes; //!< A map from a mutex's address to a vector of Mutex objects for threads waiting on it
        std::unordered_map<u64, std::vector<std::shared_ptr<WaitStatus>>> conditionals; //!< A map from a conditional variable's address to a vector of threads waiting on it
        std::vector<std::shared_ptr<TlsPage>> tlsPages; //!< A vector of all allocated TLS pages
        std::shared_ptr<KPrivateMemory> heap; //!< The kernel memory object backing the allocated heap
        Mutex mutexLock; //!< This Mutex is to prevent concurrent mutex operations to happen at once
        Mutex conditionalLock; //!< This Mutex is to prevent concurrent conditional variable operations to happen at once

        /**
         * @brief Creates a KThread object for the main thread and opens the process's memory file
         * @param state The state of the device
         * @param pid The PID of the main thread
         * @param entryPoint The address to start execution at
         * @param stackBase The base of the stack
         * @param stackSize The size of the stack
         * @param tlsMemory The KSharedMemory object for TLS memory allocated by the guest process
         */
        KProcess(const DeviceState &state, pid_t pid, u64 entryPoint, u64 stackBase, u64 stackSize, std::shared_ptr<type::KSharedMemory> &tlsMemory);

        /**
         * Close the file descriptor to the process's memory
         */
        ~KProcess();

        /**
         * @brief Create a thread in this process
         * @param entryPoint The address of the initial function
         * @param entryArg An argument to the function
         * @param stackTop The top of the stack
         * @param priority The priority of the thread
         * @return An instance of KThread class for the corresponding thread
         */
        std::shared_ptr<KThread> CreateThread(u64 entryPoint, u64 entryArg, u64 stackTop, u8 priority);

        /**
         * @brief Returns an object from process memory
         * @tparam Type The type of the object to be read
         * @param address The address of the object
         * @return An object of type T with read data
         */
        template<typename Type>
        inline Type ReadMemory(u64 address) const {
            Type item{};
            ReadMemory(&item, address, sizeof(Type));
            return item;
        }

        /**
         * @brief Writes an object to process memory
         * @tparam Type The type of the object to be written
         * @param item The object to write
         * @param address The address of the object
         */
        template<typename Type>
        inline void WriteMemory(Type &item, u64 address) const {
            WriteMemory(&item, address, sizeof(Type));
        }

        /**
         * @brief Writes an object to process memory
         * @tparam Type The type of the object to be written
         * @param item The object to write
         * @param address The address of the object
         */
        template<typename Type>
        inline void WriteMemory(const Type &item, u64 address) const {
            WriteMemory(&item, address, sizeof(Type));
        }

        /**
         * @brief Read data from the process's memory
         * @param destination The address to the location where the process memory is written
         * @param offset The address to read from in process memory
         * @param size The amount of memory to be read
         */
        void ReadMemory(void *destination, u64 offset, size_t size) const;

        /**
         * @brief Write to the process's memory
         * @param source The address of where the data to be written is present
         * @param offset The address to write to in process memory
         * @param size The amount of memory to be written
         */
        void WriteMemory(void *source, u64 offset, size_t size) const;

        /**
         * @brief Copy one chunk to another in the process's memory
         * @param source The address of where the data to read is present
         * @param destination The address to write the read data to
         * @param size The amount of memory to be copied
         */
        void CopyMemory(u64 source, u64 destination, size_t size) const;

        /**
         * @brief Creates a new handle to a KObject and adds it to the process handle_table
         * @tparam objectClass The class of the kernel object to create
         * @param args The arguments for the kernel object except handle, pid and state
         * @return A shared pointer to the corresponding object
         */
        template<typename objectClass, typename ...objectArgs>
        HandleOut<objectClass> NewHandle(objectArgs... args) {
            std::shared_ptr<objectClass> item;
            if constexpr (std::is_same<objectClass, KThread>())
                item = std::make_shared<objectClass>(state, handleIndex, args...);
            else
                item = std::make_shared<objectClass>(state, args...);
            handles[handleIndex] = std::static_pointer_cast<KObject>(item);
            return {item, handleIndex++};
        }

        /**
         * @brief Inserts an item into the process handle table
         * @param item The item to insert
         * @return The handle of the corresponding item in the handle table
         */
        template<typename objectClass>
        handle_t InsertItem(std::shared_ptr<objectClass> &item) {
            handles[handleIndex] = std::static_pointer_cast<KObject>(item);
            return handleIndex++;
        }

        /**
         * @brief Returns the underlying kernel object for a handle
         * @tparam objectClass The class of the kernel object present in the handle
         * @param handle The handle of the object
         * @return A shared pointer to the object
         */
        template<typename objectClass>
        std::shared_ptr<objectClass> GetHandle(handle_t handle) {
            KType objectType;
            if constexpr(std::is_same<objectClass, KThread>())
                objectType = KType::KThread;
            else if constexpr(std::is_same<objectClass, KProcess>())
                objectType = KType::KProcess;
            else if constexpr(std::is_same<objectClass, KSharedMemory>())
                objectType = KType::KSharedMemory;
            else if constexpr(std::is_same<objectClass, KTransferMemory>())
                objectType = KType::KTransferMemory;
            else if constexpr(std::is_same<objectClass, KPrivateMemory>())
                objectType = KType::KPrivateMemory;
            else if constexpr(std::is_same<objectClass, KSession>())
                objectType = KType::KSession;
            else if constexpr(std::is_same<objectClass, KEvent>())
                objectType = KType::KEvent;
            else
                throw exception("KProcess::GetHandle couldn't determine object type");
            try {
                auto item = handles.at(handle);
                if (item->objectType == objectType)
                    return std::static_pointer_cast<objectClass>(item);
                else
                    throw exception("Tried to get kernel object (0x{:X}) with different type: {} when object is {}", handle, objectType, item->objectType);
            } catch (std::out_of_range) {
                throw exception("GetHandle was called with invalid handle: 0x{:X}", handle);
            }
        }

        /**
         * @brief Retrieves a kernel memory object that owns the specified address
         * @param address The address to look for
         * @return A shared pointer to the corresponding KMemory object
         */
        std::optional<HandleOut<KMemory>> GetMemoryObject(u64 address);

        /**
         * @brief This deletes a certain handle from the handle table
         * @param handle The handle to delete
         */
        inline void DeleteHandle(handle_t handle) {
            handles.erase(handle);
        }

        /**
         * @brief This locks the Mutex at the specified address
         * @param address The address of the mutex
         * @param owner The handle of the current mutex owner
         * @param alwaysLock If to return rather than lock if owner tag is not matched
         */
        void MutexLock(u64 address, handle_t owner, bool alwaysLock = false);

        /**
         * @brief This unlocks the Mutex at the specified address
         * @param address The address of the mutex
         * @return If the mutex was successfully unlocked
         */
        bool MutexUnlock(u64 address);

        /**
         * @param address The address of the conditional variable
         * @param timeout The amount of time to wait for the conditional variable
         * @return If the conditional variable was successfully waited for or timed out
         */
        bool ConditionalVariableWait(u64 address, u64 timeout);

        /**
         * @brief This signals a number of conditional variable waiters
         * @param address The address of the conditional variable
         * @param amount The amount of waiters to signal
         */
        void ConditionalVariableSignal(u64 address, u64 amount);

        /**
         * @brief This resets the object to an unsignalled state
         */
        inline void ResetSignal() {
            signalled = false;
        }
    };
}
