#include <iostream>
#include <unordered_map>
#include <list>
#include <sstream>
#include <fstream>
#include <string>
#include <thread>
#include <mutex>
using namespace std;

class BlinkDB
{
private:
    unordered_map<string, string> data;                    // In-memory key-value store
    list<string> lru_list;                                 // LRU list to track usage
    unordered_map<string, list<string>::iterator> lru_map; // Map keys to LRU list iterators
    size_t capacity;                                       // Maximum number of key-value pairs in memory
    string disk_file;                                      // File to store flushed data
    string log_file;                                       // File to store all operations
    std::recursive_mutex mtx;                              // Mutex to protect shared resources

    // Evict the least recently used key if capacity is reached
    void evict()
    {
        std::lock_guard<std::recursive_mutex> lock(mtx);
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

    // Update the LRU list for a key
    void update_lru(const string &key)
    {
        std::lock_guard<std::recursive_mutex> lock(mtx);
        if (lru_map.find(key) != lru_map.end())
        {
            lru_list.erase(lru_map[key]);
        }
        lru_list.push_front(key);
        lru_map[key] = lru_list.begin();
    }

    // Flush a key-value pair to disk
    void flush_to_disk(const string &key, const string &value)
    {
        std::lock_guard<std::recursive_mutex> lock(mtx);
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

    // Log an operation to the log file
    void log_operation(const string &operation)
    {
        std::lock_guard<std::recursive_mutex> lock(mtx);
        ofstream logfile(log_file, ios::app);
        if (!logfile.is_open())
        {
            cerr << "Error: Unable to open or create the log file: " << log_file << endl;
            return;
        }
        logfile << operation << endl;
        logfile.close();
    }

    // Restore a key-value pair from disk
    string restore_from_disk(const string &key)
    {
        std::lock_guard<std::recursive_mutex> lock(mtx);
        ifstream infile(disk_file);
        if (!infile.is_open())
        {
            return "NULL"; // Disk file not accessible
        }
        string k, v;
        while (infile >> k >> v)
        {
            if (k == key)
            {
                infile.close();
                log_operation("Restored from disk: " + k + " -> " + v);
                return v;
            }
        }
        infile.close();
        return "NULL"; // Key not found on disk
    }

public:
    // Constructor with default capacity and files
    BlinkDB(size_t cap = 3, const string &disk = "blinkdb_disk_part1.txt", const string &log = "blinkdb_log_part1.txt")
        : capacity(cap), disk_file(disk), log_file(log)
    {

        // Delete existing files if they exist (original check kept unchanged)
        if (disk_file.c_str() == 0)
        {
            remove(disk_file.c_str());
        }
        if (log_file.c_str() == 0)
        {
            remove(log_file.c_str());
        }
        log_operation("BlinkDB started with capacity: " + to_string(capacity));
    }

    // Set a key-value pair
    void set(const char *key, const char *value)
    {
        std::lock_guard<std::recursive_mutex> lock(mtx);
        if (!key || !value)
        {
            cerr << "Error: Key or value is null!" << endl;
            return;
        }
        string k(key), v(value);
        evict(); // Evict if capacity is reached
        data[k] = v;
        update_lru(k);
        string log_entry = "Set: " + k + " -> " + v;
        log_operation(log_entry);
    }

    // Get the value for a key
    string get(const char *key)
    {
        std::lock_guard<std::recursive_mutex> lock(mtx);
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
            // Key not in memory, try restoring from disk
            string value = restore_from_disk(k);
            if (value != "NULL")
            {
                // Restore the key-value pair to memory
                set(k.c_str(), value.c_str());
                return value;
            }
            else
            {
                log_operation("Get: " + k + " -> NULL");
                return "NULL"; // Key not found on disk
            }
        }
    }

    // Delete a key-value pair
    void del(const char *key)
    {
        std::lock_guard<std::recursive_mutex> lock(mtx);
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
            string log_entry = "Deleted: " + k;
            log_operation(log_entry);
        }
        else
        {
            cout << "Does not exist." << endl;
            log_operation("Delete failed: " + k + " does not exist.");
        }
    }
};

// REPL for Local Interaction
void run_repl(BlinkDB &db)
{
    string input;
    while (true)
    {
        cout << "User> ";
        cout.flush(); // Ensure prompt is printed
        getline(cin, input);
        if (input.empty())
        {
            continue; // Skip empty input
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

int main(int argc, char *argv[])
{
    if (argc > 1 && string(argv[1]) == "part1")
    {
        BlinkDB db; // Default capacity and disk/log files
        // Run the REPL in a separate thread to demonstrate multithreading.
        thread repl_thread(run_repl, std::ref(db));
        repl_thread.join();
    }
    else
    {
        cout << "No valid mode specified. Run with 'part1' argument for REPL mode." << endl;
    }
    return 0;
}
