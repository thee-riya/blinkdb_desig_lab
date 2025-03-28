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
class BlinkDB {
private:
    unordered_map<string, string> data;                    ///< In-memory key-value store
    list<string> lru_list;                                 ///< LRU list to track usage
    unordered_map<string, list<string>::iterator> lru_map; ///< Map keys to LRU list iterators
    size_t capacity;                                       ///< Maximum number of key-value pairs in memory
    string disk_file;                                      ///< File to store flushed data
    string log_file;                                       ///< File to store all operations
    recursive_mutex mtx;                                   ///< Mutex to protect shared resources

    // Evict the least recently used key if capacity is reached.
    void evict() {
        lock_guard<recursive_mutex> lock(mtx);
        if (data.size() >= capacity) {
            string lru_key = lru_list.back();
            lru_list.pop_back();
            lru_map.erase(lru_key);

            // Flush the evicted key-value pair to disk.
            if (data.find(lru_key) != data.end()) {
                flush_to_disk(lru_key, data[lru_key]);
                data.erase(lru_key);
                log_operation("Evicted: " + lru_key);
            }
        }
    }

    // Update the LRU list for a key.
    void update_lru(const string &key) {
        lock_guard<recursive_mutex> lock(mtx);
        if (lru_map.find(key) != lru_map.end()) {
            lru_list.erase(lru_map[key]);
        }
        lru_list.push_front(key);
        lru_map[key] = lru_list.begin();
    }

    // Flush a key-value pair to disk.
    void flush_to_disk(const string &key, const string &value) {
        lock_guard<recursive_mutex> lock(mtx);
        ofstream outfile(disk_file, ios::app);
        if (!outfile.is_open()) {
            cerr << "Error: Unable to open or create the disk file: " << disk_file << endl;
            return;
        }
        outfile << key << " " << value << endl;
        outfile.close();
        log_operation("Flushed to disk: " + key + " -> " + value);
    }

    // Remove a key-value pair from the disk file.
    void remove_from_disk(const string &key) {
        lock_guard<recursive_mutex> lock(mtx);
        ifstream infile(disk_file);
        if (!infile.is_open()) {
            // If the file doesn't exist, nothing to remove.
            return;
        }
        vector<string> lines;
        string line;
        while (getline(infile, line)) {
            istringstream iss(line);
            string k, v;
            iss >> k >> v;
            if (k != key) {
                lines.push_back(line);
            }
        }
        infile.close();

        ofstream outfile(disk_file, ios::trunc);
        if (!outfile.is_open()) {
            cerr << "Error: Unable to open disk file for writing: " << disk_file << endl;
            return;
        }
        for (const auto &l : lines) {
            outfile << l << endl;
        }
        outfile.close();
        log_operation("Removed from disk: " + key);
    }

    // Log an operation to the log file.
    void log_operation(const string &operation) {
        lock_guard<recursive_mutex> lock(mtx);
        ofstream logfile(log_file, ios::app);
        if (!logfile.is_open()) {
            cerr << "Error: Unable to open or create the log file: " << log_file << endl;
            return;
        }
        logfile << operation << endl;
        logfile.close();
    }

    // Restore a key-value pair from disk without loading it into memory.
    string restore_from_disk(const string &key) {
        lock_guard<recursive_mutex> lock(mtx);
        ifstream infile(disk_file);
        if (!infile.is_open()) {
            return "NULL"; // Disk file not accessible
        }
        string k, v;
        bool found = false;
        while (infile >> k >> v) {
            if (k == key) {
                found = true;
                break;
            }
        }
        infile.close();
        if (found) {
            log_operation("Restored from disk: " + key + " -> " + v);
            // Remove the restored key from disk.
            remove_from_disk(key);
            return v;
        }
        return "NULL"; // Key not found on disk
    }

public:
    // Constructor for BlinkDB.
    BlinkDB(size_t cap = 3, const string &disk = "blinkdb_disk_part1.txt", const string &log = "blinkdb_log_part1.txt")
        : capacity(cap), disk_file(disk), log_file(log) {
        // Remove existing files if they exist.
        remove(disk_file.c_str());
        remove(log_file.c_str());
        log_operation("BlinkDB started with capacity: " + to_string(capacity));
    }

    // Set a key-value pair in the database.
    void set(const char *key, const char *value) {
        lock_guard<recursive_mutex> lock(mtx);
        if (!key || !value) {
            cerr << "Error: Key or value is null!" << endl;
            return;
        }
        string k(key), v(value);
        evict(); // Evict if capacity is reached.
        data[k] = v;
        update_lru(k);
        log_operation("Set: " + k + " -> " + v);
    }

    // Get the value for a key from the database.
    string get(const char *key) {
        lock_guard<recursive_mutex> lock(mtx);
        if (!key) {
            cerr << "Error: Key is null!" << endl;
            return "NULL";
        }
        string k(key);
        if (data.find(k) != data.end()) {
            update_lru(k);
            string result = data[k];
            log_operation("Get: " + k + " -> " + result);
            return result;
        }
        else {
            // Key not in memory, try restoring from disk.
            string value = restore_from_disk(k);
            if (value != "NULL") {
                // Restore the key-value pair into memory.
                set(k.c_str(), value.c_str());
                log_operation("Restored: " + k + " -> " + value);
                return value;
            }
            else {
                log_operation("Get: " + k + " -> NULL");
                return "NULL"; // Key not found on disk.
            }
        }
    }

    // Delete a key-value pair from the database.
    void del(const char *key) {
        lock_guard<recursive_mutex> lock(mtx);
        if (!key) {
            cerr << "Error: Key is null!" << endl;
            return;
        }
        string k(key);
        if (data.find(k) != data.end()) {
            data.erase(k);
            lru_list.erase(lru_map[k]);
            lru_map.erase(k);
            log_operation("Deleted (from memory): " + k);
            // Remove from disk as well.
            remove_from_disk(k);
        }
        else {
            // Key not in memory; remove it from disk.
            string value = restore_from_disk(k);
            if (value != "NULL") {
                log_operation("Deleted (from disk): " + k);
            }
            else {
                cout << "Does not exist." << endl;
                log_operation("Delete failed: " + k + " does not exist.");
            }
        }
    }
};

// Utility function to tokenize a string by whitespace.
vector<string> tokenize(const string &input) {
    vector<string> tokens;
    istringstream iss(input);
    string token;
    while (iss >> token) {
        tokens.push_back(token);
    }
    return tokens;
}

// Run a Read-Eval-Print Loop (REPL) for interacting with BlinkDB.
void run_repl(BlinkDB &db) {
    string input;
    while (true) {
        cout << "User> ";
        cout.flush();
        getline(cin, input);
        if (input.empty())
            continue;
        vector<string> tokens = tokenize(input);
        if (tokens.empty())
            continue;
        string command = tokens[0];

        if (command == "SET") {
            if (tokens.size() != 3) {
                cout << "Error: SET command requires exactly 2 arguments: key and value." << endl;
                continue;
            }
            db.set(tokens[1].c_str(), tokens[2].c_str());
        }
        else if (command == "GET") {
            if (tokens.size() != 2) {
                cout << "Error: GET command requires exactly 1 argument: key." << endl;
                continue;
            }
            cout << db.get(tokens[1].c_str()) << endl;
        }
        else if (command == "DEL") {
            if (tokens.size() != 2) {
                cout << "Error: DEL command requires exactly 1 argument: key." << endl;
                continue;
            }
            db.del(tokens[1].c_str());
        }
        else {
            cout << "Invalid command!" << endl;
        }
    }
}

// Main function: run the REPL by default.
int main() {
    BlinkDB db; // Use default capacity and disk/log file names.
    thread repl_thread(run_repl, ref(db));
    repl_thread.join();
    return 0;
}
