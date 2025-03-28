#include <iostream>
#include <unordered_map>
#include <list>
#include <sstream>
#include <fstream>
#include <string>
#include <thread>
#include <mutex>
#include <vector>
#include <cstdio>
using namespace std;

/**
 * @class BlinkDB
 * @brief A simple implementation of a key-value store with LRU eviction and persistent storage.
 *
 * This class mimics a key-value store with an in-memory cache and persistence to disk. It
 * supports basic operations such as setting, getting, and deleting key-value pairs, as well as
 * evicting the least recently used items when the capacity is exceeded. It logs all operations
 * to a log file and restores from disk when necessary.
 */
class BlinkDB
{
private:
    unordered_map<string, string> data;                    ///< In-memory key-value store
    list<string> lru_list;                                 ///< LRU list to track usage
    unordered_map<string, list<string>::iterator> lru_map; ///< Map keys to LRU list iterators
    size_t capacity;                                       ///< Maximum number of key-value pairs in memory
    string disk_file;                                      ///< File to store flushed data
    string log_file;                                       ///< File to store all operations
    recursive_mutex mtx;                                   ///< Mutex to protect shared resources

    /**
     * @brief Evict the least recently used key if capacity is reached.
     *
     * This function removes the least recently used key-value pair from memory and writes it
     * to disk if necessary.
     */
    void evict()
    {
        lock_guard<recursive_mutex> lock(mtx);
        if (data.size() >= capacity)
        {
            string lru_key = lru_list.back();
            lru_list.pop_back();
            lru_map.erase(lru_key);

            // Flush the evicted key-value pair to disk
            if (data.find(lru_key) != data.end())
            {
                flush_to_disk(lru_key, data[lru_key]);
                data.erase(lru_key);
                log_operation("Evicted: " + lru_key);
            }
        }
    }

    /**
     * @brief Update the LRU list for a key.
     *
     * This function moves the specified key to the front of the LRU list to mark it as recently used.
     *
     * @param key The key to update in the LRU list.
     */
    void update_lru(const string &key)
    {
        lock_guard<recursive_mutex> lock(mtx);
        if (lru_map.find(key) != lru_map.end())
        {
            lru_list.erase(lru_map[key]);
        }
        lru_list.push_front(key);
        lru_map[key] = lru_list.begin();
    }

    /**
     * @brief Flush a key-value pair to disk.
     *
     * This function appends the given key-value pair to the disk file.
     *
     * @param key The key to flush to disk.
     * @param value The value to flush to disk.
     */
    void flush_to_disk(const string &key, const string &value)
    {
        lock_guard<recursive_mutex> lock(mtx);
        ofstream outfile(disk_file, ios::app);
        if (!outfile.is_open())
        {
            cerr << "Error: Unable to open or create the disk file: " << disk_file << endl;
            return;
        }
        outfile << key << " " << value << endl;
        outfile.close();
        log_operation("Flushed to disk: " + key + " -> " + value);
    }

    /**
     * @brief Remove a key-value pair from the disk file.
     *
     * This function reads the disk file, removes any entry with the specified key,
     * and rewrites the file.
     *
     * @param key The key to remove from the disk.
     */
    void remove_from_disk(const string &key)
    {
        lock_guard<recursive_mutex> lock(mtx);
        ifstream infile(disk_file);
        if (!infile.is_open())
        {
            cerr << "Error: Unable to open disk file for reading: " << disk_file << endl;
            return;
        }
        vector<string> lines;
        string line;
        while (getline(infile, line))
        {
            istringstream iss(line);
            string k, v;
            iss >> k >> v;
            if (k != key)
            {
                lines.push_back(line);
            }
        }
        infile.close();

        ofstream outfile(disk_file, ios::trunc);
        if (!outfile.is_open())
        {
            cerr << "Error: Unable to open disk file for writing: " << disk_file << endl;
            return;
        }
        for (const auto &l : lines)
        {
            outfile << l << endl;
        }
        outfile.close();
        log_operation("Removed from disk: " + key);
    }

    /**
     * @brief Log an operation to the log file.
     *
     * This function appends the given operation description to the log file.
     *
     * @param operation The operation to log.
     */
    void log_operation(const string &operation)
    {
        lock_guard<recursive_mutex> lock(mtx);
        ofstream logfile(log_file, ios::app);
        if (!logfile.is_open())
        {
            cerr << "Error: Unable to open or create the log file: " << log_file << endl;
            return;
        }
        logfile << operation << endl;
        logfile.close();
    }

    /**
     * @brief Restore a key-value pair from disk.
     *
     * This function searches the disk file for the given key, removes it from disk if found, and returns the corresponding value.
     *
     * @param key The key to search for on disk.
     * @return The value associated with the key, or "NULL" if the key is not found.
     */
    string restore_from_disk(const string &key)
    {
        lock_guard<recursive_mutex> lock(mtx);
        ifstream infile(disk_file);
        if (!infile.is_open())
        {
            return "NULL"; // Disk file not accessible
        }
        string k, v;
        bool found = false;
        while (infile >> k >> v)
        {
            if (k == key)
            {
                found = true;
                break;
            }
        }
        infile.close();
        if (found)
        {
            log_operation("Restored from disk: " + key + " -> " + v);
            // Remove the restored key from disk.
            remove_from_disk(key);
            return v;
        }
        return "NULL"; // Key not found on disk
    }

public:
    /**
     * @brief Constructor for BlinkDB.
     *
     * Initializes the key-value store with a specified capacity, disk file, and log file.
     *
     * @param cap Maximum number of key-value pairs in memory (default: 3).
     * @param disk The disk file to store evicted data (default: "blinkdb_disk_part1.txt").
     * @param log The log file to store operations (default: "blinkdb_log_part1.txt").
     */
    BlinkDB(size_t cap = 3, const string &disk = "blinkdb_disk_part1.txt", const string &log = "blinkdb_log_part1.txt")
        : capacity(cap), disk_file(disk), log_file(log)
    {
        // Remove existing files if they exist.
        remove(disk_file.c_str());
        remove(log_file.c_str());
        log_operation("BlinkDB started with capacity: " + to_string(capacity));
    }

    /**
     * @brief Set a key-value pair in the database.
     *
     * This function adds a key-value pair to the in-memory store and evicts the least recently used item
     * if the capacity is exceeded.
     *
     * @param key The key to set.
     * @param value The value to associate with the key.
     */
    void set(const char *key, const char *value)
    {
        lock_guard<recursive_mutex> lock(mtx);
        if (!key || !value)
        {
            cerr << "Error: Key or value is null!" << endl;
            return;
        }
        string k(key), v(value);
        evict(); // Evict if capacity is reached
        data[k] = v;
        update_lru(k);
        log_operation("Set: " + k + " -> " + v);
    }

    /**
     * @brief Get the value for a key from the database.
     *
     * This function retrieves the value associated with the specified key. If the key is not found
     * in memory, it attempts to restore it from disk.
     *
     * @param key The key to retrieve the value for.
     * @return The value associated with the key, or "NULL" if the key does not exist.
     */
    string get(const char *key)
    {
        lock_guard<recursive_mutex> lock(mtx);
        if (!key)
        {
            cerr << "Error: Key is null!" << endl;
            return "NULL";
        }
        string k(key);
        if (data.find(k) != data.end())
        {
            update_lru(k);
            string result = data[k];
            log_operation("Get: " + k + " -> " + result);
            return result;
        }
        else
        {
            // Key not in memory, try restoring from disk.
            string value = restore_from_disk(k);
            if (value != "NULL")
            {
                // Restore the key-value pair to memory.
                set(k.c_str(), value.c_str());
                return value;
            }
            else
            {
                log_operation("Get: " + k + " -> NULL");
                return "NULL"; // Key not found on disk.
            }
        }
    }

    /**
     * @brief Delete a key-value pair from the database.
     *
     * This function removes a key-value pair from both the in-memory store and the LRU list,
     * and deletes the corresponding entry from disk.
     *
     * @param key The key to delete.
     */
    void del(const char *key)
    {
        lock_guard<recursive_mutex> lock(mtx);
        if (!key)
        {
            cerr << "Error: Key is null!" << endl;
            return;
        }
        string k(key);
        if (data.find(k) != data.end())
        {
            data.erase(k);
            lru_list.erase(lru_map[k]);
            lru_map.erase(k);
            log_operation("Deleted: " + k);
            // Remove from disk as well.
            remove_from_disk(k);
        }
        else
        {
            // Key not in memory; try to restore from disk and delete.
            string value = restore_from_disk(k);
            if (value != "NULL")
            {
                log_operation("Deleted: " + k + " (restored from disk)");
            }
            else
            {
                cout << "Does not exist." << endl;
                log_operation("Delete failed: " + k + " does not exist.");
            }
        }
    }
};

/**
 * @brief Run a Read-Eval-Print Loop (REPL) for interacting with the BlinkDB.
 *
 * This function continuously accepts user commands, processes them, and performs corresponding operations
 * on the BlinkDB instance.
 *
 * @param db The BlinkDB instance to interact with.
 */
void run_repl(BlinkDB &db)
{
    string input;
    while (true)
    {
        cout << "User> ";
        cout.flush();
        getline(cin, input);
        if (input.empty())
        {
            continue;
        }
        istringstream iss(input);
        string command, key, value;
        iss >> command >> key;

        if (command == "SET")
        {
            iss >> value;
            db.set(key.c_str(), value.c_str());
        }
        else if (command == "GET")
        {
            cout << db.get(key.c_str()) << endl;
        }
        else if (command == "DEL")
        {
            db.del(key.c_str());
        }
        else
        {
            cout << "Invalid command!" << endl;
        }
    }
}

/**
 * @brief Main function to run the BlinkDB with REPL.
 *
 * Creates a BlinkDB instance and runs the REPL in a separate thread.
 *
 * @param argc The number of command line arguments.
 * @param argv The command line arguments.
 * @return 0 on successful execution.
 */
int main(int argc, char *argv[])
{
    if (argc > 1 && string(argv[1]) == "part1")
    {
        BlinkDB db; // Use default capacity and disk/log file names.
        thread repl_thread(run_repl, ref(db));
        repl_thread.join();
    }
    else
    {
        cout << "No valid mode specified. Run with 'part1' argument for REPL mode." << endl;
    }
    return 0;
}
