#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>
#include <math.h> // For ceil, fmax
#include <ctype.h> // For isspace 

#define MAX_SPACES 50
#define MIN_DEGREE 3 // Example minimum degree for B+ Tree (Order M=2*t)
#define INPUT_FILENAME "file.txt"
#define OUTPUT_FILENAME "output.txt"

// --- Global Output File Pointer ---
FILE *outputFile = NULL;

// --- Vehicle Data ---
typedef enum {
    NO_MEMBERSHIP,
    PREMIUM,
    GOLD
} MembershipType;

const char* membership_strings[] = {"None", "Premium", "Gold"};

typedef struct {
    char vehicle_number[15]; // Key for B+ Tree (will be stored separately)
    char owner_name[50];
    time_t arrival_time;    
    time_t last_departure_time; 
    MembershipType membership;
    double total_parking_hours; 
    int num_parkings;        
    double total_amount_paid; 
    int current_parking_space_id; // -1 if not parked, otherwise 1-50
} Vehicle;

// --- Parking Space Data ---
typedef struct {
    int space_id; // Key for B+ Tree (will be stored separately)
    int status;   // 0 = free, 1 = occupied
    int occupancy_count; 
    double total_revenue; 
    char parked_vehicle_num[15]; 
} ParkingSpace;

// --- B+ Tree Node Structures ---

// Forward declarations
typedef struct BPlusTreeNode_st BPlusTreeNode;
typedef struct BPlusTree_st BPlusTree;

// Node structure (can be internal or leaf)
struct BPlusTreeNode_st {
    bool is_leaf;
    int n;          // Number of keys currently stored
    void **keys;    // Array of keys (char* for vehicle, int* for space) - Size: 2*t-1
    BPlusTree *tree; // Pointer back to the tree for config (t, compare)

    union {
        // For internal nodes
        struct {
            BPlusTreeNode **C; // Child pointers - Size: 2*t
        } internal;
        // For leaf nodes
        struct {
            void **data_pointers; // Pointers to actual Vehicle/Space data - Size: 2*t-1
            BPlusTreeNode *next; // Pointer to the next leaf node
            BPlusTreeNode *prev; // Pointer to the previous leaf node
        } leaf;
    } node_type;
};

// Tree structure
struct BPlusTree_st {
    BPlusTreeNode *root;
    int t; // Minimum degree (defines node size)
    // Comparison function pointer: returns <0 if key1<key2, 0 if key1==key2, >0 if key1>key2
    int (*compare)(const void *key1, const void *key2);
    void (*free_key)(void *key);
    void (*free_data)(void *data);
    BPlusTreeNode *first_leaf; // Pointer to the start of the leaf list (for easier traversal)
    size_t key_size; // Size of the key type 
};

// --- Structures for Sorting/Reporting 
typedef struct ReportVehicleNode {
    Vehicle *vehicle;
    struct ReportVehicleNode *next;
} ReportVehicleNode;

typedef struct ReportSpaceNode {
    ParkingSpace *space;
    struct ReportSpaceNode *next;
} ReportSpaceNode;


// --- Helper Function Prototypes ---
void safe_strcpy(char *dest, const char *src, size_t dest_size);
void clearInputBuffer();
void formatTime(time_t rawtime, char* buffer, size_t buffer_size);
time_t parseDateTimeString(const char* date_str, const char* time_str, const char* ampm_str);
time_t parseUserInputDateTime(const char* datetime_str); 
char* trim_whitespace(char *str);
int compare_vehicle_keys(const void *key1, const void *key2); 
int compare_space_keys(const void *key1, const void *key2);   
void free_vehicle_key(void *key); 
void free_space_key(void *key);   
void free_vehicle_data(void *data); 
void free_space_data(void *data);   
void* create_vehicle_key(const char* vnum); // Duplicate string key
void* create_space_key(int space_id); // Allocate and store int key

// --- B+ Tree Core Function Prototypes ---
BPlusTreeNode* createBPlusTreeNode(BPlusTree *tree, bool is_leaf);
BPlusTree* createBPlusTree(int t, size_t key_size,   int (*compare)(const void*, const void*),
                           void (*free_key)(void*),   void (*free_data)(void*));
void* searchBPlusTree(BPlusTree *tree, const void *key); // Returns data pointer or NULL
BPlusTreeNode* findLeaf(BPlusTreeNode *node, const void *key);
void insertBPlusTree(BPlusTree *tree, void *key, void *data_ptr);
void insertIntoLeaf(BPlusTreeNode *leaf, void *key, void *data_ptr);
void insertIntoParent(BPlusTreeNode *node, void *key, BPlusTreeNode *right_child); 
void insertIntoInternal(BPlusTreeNode *node, void *key, BPlusTreeNode *right_child); // Helper for insertIntoParent

// --- Parking System Logic Function Prototypes ---
void updateMembership(Vehicle *v);
void loadInitialData(BPlusTree *vehicleTree, BPlusTree *spaceTree);
int findAvailableSpace(BPlusTree *spaceTree, MembershipType membership);
int findSpaceInRangeFromLeaves(BPlusTree *spaceTree, int start_id, int end_id); // Helper
void handleVehicleEntry(BPlusTree *vehicleTree, BPlusTree *spaceTree);
void handleVehicleExit(BPlusTree *vehicleTree, BPlusTree *spaceTree);
double calculateParkingFee(double hours, MembershipType membership);
void displayVehicleDetails(const Vehicle *v); // Writes to outputFile
void displaySpaceDetails(const ParkingSpace *ps); // Writes to outputFile

// --- Reporting List Helper Function Prototypes ---
ReportVehicleNode* insertSortedVehicleByParkings(ReportVehicleNode *head, Vehicle *v);
ReportVehicleNode* insertSortedVehicleByAmount(ReportVehicleNode *head, Vehicle *v);
ReportSpaceNode* insertSortedSpaceByOccupancy(ReportSpaceNode *head, ParkingSpace *ps);
ReportSpaceNode* insertSortedSpaceByRevenue(ReportSpaceNode *head, ParkingSpace *ps);
void collectVehiclesSorted(BPlusTree *tree, ReportVehicleNode **listHead, int sortType);
void collectSpacesSorted(BPlusTree *tree, ReportSpaceNode **listHead, int sortType);
void freeReportVehicleList(ReportVehicleNode *head);
void freeReportSpaceList(ReportSpaceNode *head);

// --- Memory Management Function Prototypes ---
void destroyBPlusTreeNodeRecursive(BPlusTreeNode *node);
void destroyBPlusTree(BPlusTree *tree);

// --- Main Function ---
int main() {
    outputFile = fopen(OUTPUT_FILENAME, "w");
    if (!outputFile) {
        perror(" ERROR: Could not open output file");
        // Cannot log to file, print to stderr
        fprintf(stderr, " ERROR: Could not open output file '%s'. Exiting.\n", OUTPUT_FILENAME);
        return EXIT_FAILURE;
    }

    fprintf(outputFile, "--- Smart Car Parking System Initializing ---\n");
    fprintf(outputFile, "Timestamp: %ld\n", time(NULL)); // Add timestamp
    printf("Smart Car Parking System\n");
    printf("Output is being written to %s\n", OUTPUT_FILENAME);
    // Using key_size = 0 as placeholder, since we handle allocation based on type
    BPlusTree *vehicleTree = createBPlusTree(MIN_DEGREE, 0,     compare_vehicle_keys,
                                             free_vehicle_key,     free_vehicle_data);
    BPlusTree *spaceTree = createBPlusTree(MIN_DEGREE, sizeof(int),   compare_space_keys,
                                           free_space_key,   free_space_data);

    if (!vehicleTree || !spaceTree) {
       //  fprintf(stderr, " ERROR: Could not create B+ Trees.\n");
         fprintf(outputFile, " ERROR: Could not create B+ Trees.\n");
         fclose(outputFile);
         destroyBPlusTree(vehicleTree); // Safe even if NULL
         destroyBPlusTree(spaceTree); // Safe even if NULL
         return EXIT_FAILURE;
    }
    // Load initial data
    loadInitialData(vehicleTree, spaceTree);

    int choice;
    do {
        printf("\n--- Smart Car Parking System Menu ---\n");
        printf("1. Vehicle Entry\n");
        printf("2. Vehicle Exit\n");
        printf("3. Print Vehicles by Parking Count (to %s)\n", OUTPUT_FILENAME);
        printf("4. Print Vehicles by Amount Paid [Range] (to %s)\n", OUTPUT_FILENAME);
        printf("5. Print Spaces by Occupancy Count (to %s)\n", OUTPUT_FILENAME);
        printf("6. Print Spaces by Revenue (to %s)\n", OUTPUT_FILENAME);
        printf("7. Print All Vehicle Details (to %s)\n", OUTPUT_FILENAME);
        printf("8. Print All Space Details (to %s)\n", OUTPUT_FILENAME);
        printf("0. Exit\n");
        printf("Enter your choice: ");

        if (scanf("%d", &choice) != 1) {
            fprintf(stderr, "Invalid input. Please enter a number.\n");
            clearInputBuffer(); // Clear invalid input
            choice = -1; 
            continue;
        }
        clearInputBuffer(); // Consume the newline character after scanf

        fprintf(outputFile, "\n>>> User selected option: %d <<<\n", choice);

        switch (choice) {
            case 1:
                handleVehicleEntry(vehicleTree, spaceTree);
                break;
            case 2:
                handleVehicleExit(vehicleTree, spaceTree);
                break;
            case 3: // Print Vehicles by Parking Count
                {
                    fprintf(outputFile, "\n--- Vehicles Sorted by Number of Parkings (Descending) ---\n");
                    ReportVehicleNode *listHead = NULL;
                    collectVehiclesSorted(vehicleTree, &listHead, 1); // 1 for parking count sort
                    ReportVehicleNode *current = listHead;
                    if (!current) {
                        fprintf(outputFile, "No vehicle data available.\n");
                    } else {
                        while (current) {
                            displayVehicleDetails(current->vehicle);
                            current = current->next;
                        }
                    }
                    freeReportVehicleList(listHead);
                    fprintf(outputFile, "--- End of Report ---\n");
                    printf("Report generated in %s\n", OUTPUT_FILENAME); // Console feedback
                }
                break;
            case 4: // Print Vehicles by Amount Paid (Range)
                {
                    double min_amount, max_amount;
                    fprintf(outputFile, "\n--- Report: Vehicles by Amount Paid Range ---\n");
                    printf("Enter minimum total amount paid: "); // Console prompt
                    if (scanf("%lf", &min_amount) != 1) {
                        fprintf(stderr, "Invalid input for minimum amount.\n"); clearInputBuffer();
                        fprintf(outputFile, "Error: Invalid input for minimum amount.\n"); continue;
                    } clearInputBuffer();
                    printf("Enter maximum total amount paid: "); // Console prompt
                     if (scanf("%lf", &max_amount) != 1) {
                        fprintf(stderr, "Invalid input for maximum amount.\n"); clearInputBuffer();
                        fprintf(outputFile, "Error: Invalid input for maximum amount.\n"); continue;
                    } clearInputBuffer();

                    if (min_amount < 0 || max_amount < 0 || min_amount > max_amount) {
                        fprintf(outputFile, "Error: Invalid amount range (must be non-negative, min <= max).\n");
                        printf("Error: Invalid amount range.\n"); // Console feedback
                        continue;
                    }
                    fprintf(outputFile, "--- Vehicles with Total Amount Paid between %.2f and %.2f (Sorted Descending by Amount) ---\n", min_amount, max_amount);
                    ReportVehicleNode *listHead = NULL;
                    collectVehiclesSorted(vehicleTree, &listHead, 2); // 2 for amount paid sort
                    ReportVehicleNode *current = listHead;
                    int count = 0;
                    if (!current) {
                         fprintf(outputFile, "No vehicle data available.\n");
                    } else {
                        while (current) {
                            if (current->vehicle && current->vehicle->total_amount_paid >= min_amount && current->vehicle->total_amount_paid <= max_amount) {
                                displayVehicleDetails(current->vehicle);
                                count++;
                            }
                            current = current->next;
                        }
                    }
                     if (count == 0) {
                        fprintf(outputFile, "No vehicles found within the specified amount range.\n");
                    }
                    freeReportVehicleList(listHead);
                    fprintf(outputFile, "--- End of Report ---\n");
                    printf("Report generated in %s\n", OUTPUT_FILENAME); // Console feedback
                }
                break;
            case 5: // Print Spaces by Occupancy Count
                {
                    fprintf(outputFile, "\n--- Parking Spaces Sorted by Occupancy Count (Descending) ---\n");
                    ReportSpaceNode *listHead = NULL;
                    collectSpacesSorted(spaceTree, &listHead, 1); // 1 for occupancy sort
                    ReportSpaceNode *current = listHead;
                     if (!current) {
                        fprintf(outputFile, "No parking space data available.\n");
                    } else {
                        while (current) {
                            displaySpaceDetails(current->space);
                            current = current->next;
                        }
                    }
                    freeReportSpaceList(listHead);
                    fprintf(outputFile, "--- End of Report ---\n");
                    printf("Report generated in %s\n", OUTPUT_FILENAME); // Console feedback
                }
                break;
            case 6: // Print Spaces by Revenue
                {
                    fprintf(outputFile, "\n--- Parking Spaces Sorted by Total Revenue (Descending) ---\n");
                     ReportSpaceNode *listHead = NULL;
                    collectSpacesSorted(spaceTree, &listHead, 2); // 2 for revenue sort
                    ReportSpaceNode *current = listHead;
                     if (!current) {
                        fprintf(outputFile, "No parking space data available.\n");
                    } else {
                        while (current) {
                            displaySpaceDetails(current->space);
                            current = current->next;
                        }
                    }
                    freeReportSpaceList(listHead);
                    fprintf(outputFile, "--- End of Report ---\n");
                    printf("Report generated in %s\n", OUTPUT_FILENAME); // Console feedback
                }
                break;
            case 7: // Print All Vehicle Details (Unsorted)
                 {
                     fprintf(outputFile, "\n--- All Vehicle Details (Leaf Order) ---\n");
                     ReportVehicleNode *allVehicles = NULL;
                     collectVehiclesSorted(vehicleTree, &allVehicles, 0); // Use sortType 0 for unsorted append
                     ReportVehicleNode *tempV = allVehicles;
                     if (!tempV) { fprintf(outputFile, "No vehicles in the system.\n"); }
                     else {
                         while(tempV) { displayVehicleDetails(tempV->vehicle); tempV = tempV->next; }
                     }
                     freeReportVehicleList(allVehicles);
                     fprintf(outputFile, "--- End of List ---\n");
                     printf("List generated in %s\n", OUTPUT_FILENAME); // Console feedback
                 }
                break;
            case 8: // Print All Space Details (Unsorted)
                 {
                     fprintf(outputFile, "\n--- All Space Details (Leaf Order) ---\n");
                     ReportSpaceNode *allSpaces = NULL;
                     collectSpacesSorted(spaceTree, &allSpaces, 0); // Use sortType 0 for unsorted append
                     ReportSpaceNode *tempS = allSpaces;
                      if (!tempS) { fprintf(outputFile, "No spaces initialized (Error?).\n"); }
                      else {
                         while(tempS) { displaySpaceDetails(tempS->space); tempS = tempS->next; }
                      }
                     freeReportSpaceList(allSpaces);
                     fprintf(outputFile, "--- End of List ---\n");
                     printf("List generated in %s\n", OUTPUT_FILENAME); // Console feedback
                 }
                break;
            case 0:
                printf("Exiting system. Final output in %s. Cleaning up...\n", OUTPUT_FILENAME);
                fprintf(outputFile, "\n--- Exiting System ---\n");
                break;
            default:
                 printf("Invalid choice. Please try again.\n");
                 fprintf(outputFile, "Invalid choice entered: %d\n", choice);
        }
        fflush(outputFile); // Ensure output is written promptly after each operation
    } while (choice != 0);

    // Cleanup
    destroyBPlusTree(vehicleTree);
    destroyBPlusTree(spaceTree);
    printf("Closing complete. Goodbye!\n");

    // Close output file
    if (outputFile) {
        fclose(outputFile);
        outputFile = NULL; // Set to NULL after closing
    }
    return 0;
}

// --- Helper Function Implementations ---
void safe_strcpy(char *dest, const char *src, size_t dest_size) {
    if (!dest || !src || dest_size == 0) return;
    strncpy(dest, src, dest_size - 1);
    dest[dest_size - 1] = '\0'; // Ensure null termination
}

void clearInputBuffer() {
    int c;
    while ((c = getchar()) != '\n' && c != EOF);
}

void formatTime(time_t rawtime, char* buffer, size_t buffer_size) {
    if (!buffer || buffer_size == 0) return;
    if (rawtime == 0) {
        safe_strcpy(buffer, "N/A", buffer_size);
        return;
    }
    struct tm * timeinfo;
    timeinfo = localtime(&rawtime);
    if (timeinfo) {
        strftime(buffer, buffer_size, "%Y-%m-%d %H:%M:%S", timeinfo);
    } else {
        safe_strcpy(buffer, "Invalid Time", buffer_size);
    }
}

// Parses "DD-MM-YYYY", "HH:MM", "AM/PM" into time_t
time_t parseDateTimeString(const char* date_str, const char* time_str, const char* ampm_str) {
    if (!date_str || !time_str || !ampm_str ||
        strcmp(date_str, "none") == 0 || strcmp(time_str, "none") == 0 ||
        strcmp(ampm_str, "none") == 0 || strcmp(ampm_str, "nonnone") == 0) { // Handle "none" and typo
        return 0; 
    }

    struct tm t = {0};
    int hour = 0, min = 0;

    if (sscanf(date_str, "%d-%d-%d", &t.tm_mday, &t.tm_mon, &t.tm_year) == 3) {
        if (t.tm_year < 1900) { 
             fprintf(stderr, "Warning: Suspiciously old year (%d) parsed from file for date %s.\n", t.tm_year, date_str);
             // Allow it for now, but mktime might fail later if year is too small
        }
        t.tm_mon -= 1; // struct tm months are 0-11
        t.tm_year -= 1900; 
    } else {
        fprintf(stderr, " Invalid date format '%s' in input file.\n", date_str);
        return 0;
    }

    if (sscanf(time_str, "%d:%d", &hour, &min) == 2) {
        if (hour < 0 || hour > 23 || min < 0 || min > 59) { 
             fprintf(stderr, " Invalid time values (%d:%d) parsed from file for time %s.\n", hour, min, time_str);
             // Allow conversion attempt, mktime might handle/adjust
        }
        t.tm_min = min;
    } else {
         fprintf(stderr, " Invalid time format '%s' in input file.\n", time_str);
        return 0; 
    }

    if (hour < 1 || hour > 12) {
         fprintf(stderr, "Error: Hour (%d) out of 1-12 range for AM/PM format in time '%s'. Assuming 12-hour format.\n", hour, time_str);
         // Attempt to correct common errors, e.g., 0 PM -> 12 PM? No, just proceed.
    }

    if (strcasecmp(ampm_str, "PM") == 0 && hour != 12) {
        hour += 12;
    } else if (strcasecmp(ampm_str, "AM") == 0 && hour == 12) { // Midnight case (12 AM -> 00:xx)
        hour = 0;
    } else if (strcasecmp(ampm_str, "AM") != 0 && strcasecmp(ampm_str, "PM") != 0) {
         fprintf(stderr, " Invalid AM/PM designator '%s' in input file.\n", ampm_str);
         return 0;
    }
    // Final check on adjusted hour
    if (hour < 0 || hour > 23) {
         fprintf(stderr, "Error: Calculated hour (%d) is invalid after AM/PM adjustment.\n", hour);
         return 0;
    }
    t.tm_hour = hour;
    t.tm_isdst = -1; 
    time_t result = mktime(&t);
    if (result == -1) {
         fprintf(stderr, "Error: mktime failed to convert date/time: %s %s %s\n", date_str, time_str, ampm_str);
         return 0;
    }
    return result;
}

// Parses "YYYY-MM-DD HH:MM:SS" (24-hour) from user input using sscanf
time_t parseUserInputDateTime(const char* datetime_str) {
    if (!datetime_str) return 0;
    struct tm t = {0};
    int year, month, day, hour, min, sec;
    if (sscanf(datetime_str, "%d-%d-%d %d:%d:%d",   &year, &month, &day, &hour, &min, &sec) == 6) {
        if (year < 1900 || month < 1 || month > 12 || day < 1 || day > 31 ||
            hour < 0 || hour > 23 || min < 0 || min > 59 || sec < 0 || sec > 59)
        {
            fprintf(stderr, "Error: Invalid date/time component value in input '%s'.\n", datetime_str);
            return 0;
        }

        t.tm_year = year - 1900;
        t.tm_mon = month - 1; // struct tm months are 0-11
        t.tm_mday = day;
        t.tm_hour = hour;
        t.tm_min = min;
        t.tm_sec = sec;
        t.tm_isdst = -1; // Let mktime determine DST

        time_t result = mktime(&t);
        if (result == (time_t)-1) {
             fprintf(stderr, "Error: mktime failed to convert user input '%s'. Date/time likely invalid.\n", datetime_str);
             return 0;
        }
        return result;

    } else {
        fprintf(stderr, "Error: Invalid date/time format provided by user. Expected YYYY-MM-DD HH:MM:SS, got '%s'.\n", datetime_str);
        return 0; 
    }
}

// Removes leading/trailing whitespace
char *trim_whitespace(char *str) {
    if (!str) return NULL;
    char *end;
    while (isspace((unsigned char)*str)) str++;

    if (*str == 0) // All spaces?
        return str;

    // Trim trailing space
    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;

    // Write new null terminator character
    end[1] = '\0';
    return str;
}

// --- Comparison and Freeing Functions ---

int compare_vehicle_keys(const void *key1, const void *key2) {
    // check for NULL keys
    if (!key1 && !key2) return 0;
    if (!key1) return -1; // Treat NULL as less than non-NULL
    if (!key2) return 1;
    return strcmp((const char*)key1, (const char*)key2);
}

int compare_space_keys(const void *key1, const void *key2) {
    // check for NULL keys
    if (!key1 && !key2) return 0;
    if (!key1) return -1;
    if (!key2) return 1;
    int id1 = *(const int*)key1;
    int id2 = *(const int*)key2;
    if (id1 < id2) return -1;
    if (id1 > id2) return 1;
    return 0;
}

void free_vehicle_key(void *key) {
    free(key); // Free the duplicated string
}

void free_space_key(void *key) {
    free(key); // Free the allocated int pointer
}

void free_vehicle_data(void *data) {
    free(data); // Free the Vehicle struct
}

void free_space_data(void *data) {
    free(data); // Free the ParkingSpace struct
}

void* create_vehicle_key(const char* vnum) {
    if (!vnum) return NULL;
    char *key = malloc(strlen(vnum) + 1);
    if (key) {
        strcpy(key, vnum);
    } else {
     //   perror(" Failed to allocate vehicle key");
        fprintf(outputFile, " Failed to allocate memory for vehicle key '%s'. Exiting.\n", vnum);
        fclose(outputFile); //  close file before exit
       // exit(EXIT_FAILURE); // Critical error
    }
    return key;
}

void* create_space_key(int space_id) {
    int *key = malloc(sizeof(int));
    if (key) {
        *key = space_id;
    } else {
      //   perror(" Failed to allocate space key");
         fprintf(outputFile, " Failed to allocate memory for space key %d. Exiting.\n", space_id);
         fclose(outputFile);
        // exit(EXIT_FAILURE); // Critical error
    }
    return key;
}


// --- B+ Tree Core Implementations ---
BPlusTreeNode* createBPlusTreeNode(BPlusTree *tree, bool is_leaf) {
    BPlusTreeNode *node = (BPlusTreeNode*)malloc(sizeof(BPlusTreeNode));
    if (!node) {
    //    perror(" Failed to allocate B+ Tree node");
        fprintf(outputFile, " Failed to allocate B+ Tree node. Exiting.\n");
        if (outputFile) fclose(outputFile);
      //  exit(EXIT_FAILURE);
    }
    node->is_leaf = is_leaf;
    node->n = 0;
    node->tree = tree;
    int t = tree->t;

    // Allocate keys array (max 2t-1 keys)
    node->keys = calloc(2 * t - 1, sizeof(void*)); // Use calloc for NULL initialization
    if (!node->keys) {
       // perror(" Failed to allocate keys array");
        fprintf(outputFile, " Failed to allocate keys array. Exiting.\n");
        free(node);
        if (outputFile) 
         fclose(outputFile);
     //   exit(EXIT_FAILURE);
    }

    if (is_leaf) {
        // Allocate data pointers array (max 2t-1 data pointers)
        node->node_type.leaf.data_pointers = calloc(2 * t - 1, sizeof(void*));
        if (!node->node_type.leaf.data_pointers) {
            //perror(" Failed to allocate data pointers array");
            fprintf(outputFile, " Failed to allocate data pointers array. Exiting.\n");
            free(node->keys); free(node);
            if (outputFile) fclose(outputFile);
          //  exit(EXIT_FAILURE);
        }
        node->node_type.leaf.next = NULL;
        node->node_type.leaf.prev = NULL;
    } else {
        // Allocate child pointers array (max 2t children)
        node->node_type.internal.C = calloc(2 * t, sizeof(BPlusTreeNode*));
         if (!node->node_type.internal.C) {
          //  perror(" Failed to allocate child pointers array");
            fprintf(outputFile, " Failed to allocate child pointers array. Exiting.\n");
            free(node->keys); free(node);
            if (outputFile) fclose(outputFile);
        //    exit(EXIT_FAILURE);
        }
    }
    return node;
}

BPlusTree* createBPlusTree(int t, size_t key_size,   int (*compare)(const void*, const void*),
                           void (*free_key)(void*),   void (*free_data)(void*)) {
    if (t < 2) {
       // fprintf(stderr, "Error: B+ Tree minimum degree t must be at least 2.\n");
        fprintf(outputFile, "Error: B+ Tree minimum degree t must be at least 2.\n");
        return NULL;
    }
    BPlusTree *tree = (BPlusTree*)malloc(sizeof(BPlusTree));
    if (!tree) {
     //   perror(" Failed to allocate B+ Tree structure");
        fprintf(outputFile, " Failed to allocate B+ Tree structure. Exiting.\n");
        if (outputFile) fclose(outputFile);
       // exit(EXIT_FAILURE);
    }

    tree->t = t;
    tree->compare = compare;
    tree->free_key = free_key;
    tree->free_data = free_data;
    tree->key_size = key_size; // Store key size
    tree->root = createBPlusTreeNode(tree, true); 
    tree->first_leaf = tree->root; 

    return tree;
}

// Finds the leaf node where the key *should* exist or be inserted
BPlusTreeNode* findLeaf(BPlusTreeNode *node, const void *key) {
    if (!node || !key) return NULL; // NULL check for key
    BPlusTree *tree = node->tree;
    if (!tree) return NULL; 
    BPlusTreeNode *current_node = node; // Use a temporary variable
    while (current_node && !current_node->is_leaf) {
        int i = 0;
        // Find the first key greater than the search key
        while (i < current_node->n && tree->compare(key, current_node->keys[i]) >= 0) {
            i++;
        }
        // Follow the child pointer C[i]
        if (i > current_node->n || !current_node->node_type.internal.C) {
          //   fprintf(stderr, "Error: Corrupted internal node detected during findLeaf.\n");
             fprintf(outputFile, "Error: Corrupted internal node detected during findLeaf.\n");
             return NULL; 
        }
        current_node = current_node->node_type.internal.C[i];
    }
    // Return NULL if traversal failed
    return current_node && current_node->is_leaf ? current_node : NULL;
}


// Searches the B+ Tree for a key and returns the associated data pointer
void* searchBPlusTree(BPlusTree *tree, const void *key) {
    if (!tree || !tree->root || !key) 
       return NULL;
    BPlusTreeNode *leaf = findLeaf(tree->root, key);
    if (!leaf) return NULL;

    // Linear search within the leaf node
    for (int i = 0; i < leaf->n; i++) {
        if (tree->compare(key, leaf->keys[i]) == 0) {
            return (leaf->node_type.leaf.data_pointers && i < (2 * tree->t - 1))
                   ? leaf->node_type.leaf.data_pointers[i]
                   : NULL;
        }
    }
    return NULL; 
}

// Inserts a key-data pair into the leaf node, maintaining sorted order
void insertIntoLeaf(BPlusTreeNode *leaf, void *key, void *data_ptr) {
    if (!leaf || !leaf->is_leaf || !key || !data_ptr) return; 
    BPlusTree *tree = leaf->tree;
    if (!tree) return; // Node must belong to a tree
    int i = leaf->n - 1;
    if (!leaf->keys || !leaf->node_type.leaf.data_pointers) {
       // fprintf(stderr, "Error: Corrupted leaf node (missing arrays) in insertIntoLeaf.\n");
        fprintf(outputFile, "Error: Corrupted leaf node (missing arrays) in insertIntoLeaf.\n");
        return;
    }

    // Find position to insert (maintaining sorted order)
    while (i >= 0 && tree->compare(key, leaf->keys[i]) < 0) {
        if (i + 1 >= 2 * tree->t - 1) {
       //      fprintf(stderr, "Error: Index out of bounds during shift in insertIntoLeaf.\n");
             fprintf(outputFile, "Error: Index out of bounds during shift in insertIntoLeaf.\n");
             return; // Avoid buffer overflow
        }
        leaf->keys[i + 1] = leaf->keys[i];
        leaf->node_type.leaf.data_pointers[i + 1] = leaf->node_type.leaf.data_pointers[i];
        i--;
    }

    // Insert new key and data pointer at index i+1
     if (i + 1 >= 2 * tree->t - 1) {
      //   fprintf(stderr, "Error: Index out of bounds for insertion in insertIntoLeaf.\n");
         fprintf(outputFile, "Error: Index out of bounds for insertion in insertIntoLeaf.\n");
         return; // Avoid buffer overflow
     }
    leaf->keys[i + 1] = key;
    leaf->node_type.leaf.data_pointers[i + 1] = data_ptr;
    leaf->n++;
}

// Inserts a key and a new right child into an internal node
void insertIntoInternal(BPlusTreeNode *node, void *key, BPlusTreeNode *right_child) {
     if (!node || node->is_leaf || !key || !right_child) return; // Basic validation
     BPlusTree *tree = node->tree;
     if (!tree) return;
     int i = node->n - 1;

     // Check if arrays are valid
     if (!node->keys || !node->node_type.internal.C) {
       // fprintf(stderr, "Error: Corrupted internal node (missing arrays) in insertIntoInternal.\n");
        fprintf(outputFile, "Error: Corrupted internal node (missing arrays) in insertIntoInternal.\n");
        return;
     }

     // Find position for the new key
     while (i >= 0 && tree->compare(key, node->keys[i]) < 0) {
         // Ensure indices i+1 (for key) and i+2 (for child) are within bounds
         if (i + 1 >= 2 * tree->t - 1 || i + 2 >= 2 * tree->t) {
        //     fprintf(stderr, "Error: Index out of bounds during shift in insertIntoInternal.\n");
             fprintf(outputFile, "Error: Index out of bounds during shift in insertIntoInternal.\n");
             return; 
         }
         node->keys[i + 1] = node->keys[i];
         node->node_type.internal.C[i + 2] = node->node_type.internal.C[i + 1]; // Shift children pointers too
         i--;
     }

     // Insert key and the new right child pointer at index i+1 and i+2 respectively
     if (i + 1 >= 2 * tree->t - 1 || i + 2 >= 2 * tree->t) {
  //       fprintf(stderr, "Error: Index out of bounds for insertion in insertIntoInternal.\n");
         fprintf(outputFile, "Error: Index out of bounds for insertion in insertIntoInternal.\n");
         return; // Avoid buffer overflow
     }
     node->keys[i + 1] = key;
     node->node_type.internal.C[i + 2] = right_child;
     node->n++;
}


// Splits a full leaf node into two, updates linked list, and returns the middle key and new node
void splitLeafNode(BPlusTreeNode *leaf, void **key_to_push_up, BPlusTreeNode **new_leaf_node) {
    if (!leaf || !leaf->is_leaf || !key_to_push_up || !new_leaf_node) return;
    BPlusTree *tree = leaf->tree;
    if (!tree) return;
    int t = tree->t;

    *new_leaf_node = createBPlusTreeNode(tree, true);
    if (!*new_leaf_node) { 
        *key_to_push_up = NULL;
        return;
    }
    BPlusTreeNode *new_leaf = *new_leaf_node;

    int split_point = t; 
    // The key to copy up is the *first* key of the new node
    // Allocate space for the key copy
    size_t key_alloc_size = (tree->compare == compare_vehicle_keys) ? (strlen((char*)leaf->keys[split_point]) + 1) : tree->key_size;
    *key_to_push_up = malloc(key_alloc_size);
    if (!*key_to_push_up) {
      //  perror(" Failed to allocate key for push up");
        fprintf(outputFile, " Failed to allocate key for push up during leaf split. Exiting.\n");
        // Cleanup attempt
        free(new_leaf->keys);
        free(new_leaf->node_type.leaf.data_pointers);
        free(new_leaf);
        *new_leaf_node = NULL;
        if (outputFile) fclose(outputFile);
      //  exit(EXIT_FAILURE);
    }

    // Copy the key value
    if (tree->compare == compare_vehicle_keys) {
        strcpy((char*)*key_to_push_up, (char*)leaf->keys[split_point]);
    } else { 
        memcpy(*key_to_push_up, leaf->keys[split_point], tree->key_size);
    }
    // Move the second half of keys and data pointers to the new leaf
    new_leaf->n = 0;
    for (int i = split_point; i < 2 * t - 1; i++) {
        if (new_leaf->n >= 2 * t - 1) {
           //  fprintf(stderr, "Error: Exceeded new leaf capacity during split.\n");
             fprintf(outputFile, "Error: Exceeded new leaf capacity during split.\n");
             // Potential memory leak of *key_to_push_up
             if (tree->free_key) tree->free_key(*key_to_push_up);
             *key_to_push_up = NULL;
             // Tree might be inconsistent
             return;
        }
        new_leaf->keys[new_leaf->n] = leaf->keys[i];
        new_leaf->node_type.leaf.data_pointers[new_leaf->n] = leaf->node_type.leaf.data_pointers[i];
        leaf->keys[i] = NULL; // Null out moved pointers in old leaf
        leaf->node_type.leaf.data_pointers[i] = NULL;
        new_leaf->n++;
    }
    // Update original leaf's key count
    leaf->n = split_point;

    // Update linked list pointers
    new_leaf->node_type.leaf.next = leaf->node_type.leaf.next;
    if (new_leaf->node_type.leaf.next != NULL) {
        new_leaf->node_type.leaf.next->node_type.leaf.prev = new_leaf;
    }
    leaf->node_type.leaf.next = new_leaf;
    new_leaf->node_type.leaf.prev = leaf;
}

// Splits a full internal node into two, pushes middle key up, returns key and new node
void splitInternalNode(BPlusTreeNode *node, void **key_to_push_up, BPlusTreeNode **new_internal_node) {
    if (!node || node->is_leaf || !key_to_push_up || !new_internal_node) return;
    BPlusTree *tree = node->tree;
     if (!tree) return;
    int t = tree->t;
    *new_internal_node = createBPlusTreeNode(tree, false);
     if (!*new_internal_node) { 
        *key_to_push_up = NULL;
        return;
    }
    BPlusTreeNode *new_node = *new_internal_node;
    // Middle key index (key at t-1 is pushed up)
    int split_key_index = t - 1;

    // Key to push up is the middle key
    *key_to_push_up = node->keys[split_key_index]; // Pass pointer up
    node->keys[split_key_index] = NULL; // Remove from original node

    // Move keys from index t onwards to the new node
    new_node->n = 0;
    for (int i = split_key_index + 1; i < 2 * t - 1; i++) {
         if (new_node->n >= 2 * t - 1) {
           //  fprintf(stderr, "Error: Exceeded new internal node key capacity during split.\n");
             fprintf(outputFile, "Error: Exceeded new internal node key capacity during split.\n");
             // Key *key_to_push_up might leak if not handled by caller
             return;
         }
        new_node->keys[new_node->n++] = node->keys[i];
        node->keys[i] = NULL;
    }

    // Move child pointers from index t onwards to the new node
    for (int i = t; i < 2 * t; i++) {
         if (i - t >= 2 * t) {
           //  fprintf(stderr, "Error: Exceeded new internal node child capacity during split.\n");
             fprintf(outputFile, "Error: Exceeded new internal node child capacity during split.\n");
             return;
         }
        new_node->node_type.internal.C[i - t] = node->node_type.internal.C[i];
        node->node_type.internal.C[i] = NULL;
    }
    // Update original node's key count
    node->n = split_key_index; // t-1 keys remain
}

// Main insertion function
void insertBPlusTree(BPlusTree *tree, void *key, void *data_ptr) {
    if (!tree || !key || !data_ptr) {
       // fprintf(stderr, "Error: Invalid arguments for insertBPlusTree.\n");
        fprintf(outputFile, "Error: Invalid arguments for insertBPlusTree (key=%p, data=%p).\n", key, data_ptr);
        return;
    }
    // Handle empty tree case
    if (tree->root == NULL) { 
         fprintf(outputFile, "Error: Tree root was NULL during insert. Recreating root.\n");
         tree->root = createBPlusTreeNode(tree, true);
         tree->first_leaf = tree->root;
    }
    if (tree->root->n == 0 && tree->root->is_leaf) {
        tree->root->keys[0] = key;
        tree->root->node_type.leaf.data_pointers[0] = data_ptr;
        tree->root->n = 1;
        return;
    }

    // Find the appropriate leaf node
    BPlusTreeNode *leaf = findLeaf(tree->root, key);
    if (!leaf) {
     //   fprintf(stderr, "Error: Could not find leaf node for insertion.\n");
        fprintf(outputFile, "Error: Could not find leaf node for insertion.\n");
         // Free key/data as insertion failed
         if (key && tree->free_key) tree->free_key(key);
         if (data_ptr && tree->free_data) tree->free_data(data_ptr);
        return;
    }

    // Check for duplicates *before* insertion/split
    for (int i = 0; i < leaf->n; i++) {
        if (tree->compare(key, leaf->keys[i]) == 0) {
         //   fprintf(stderr, "Error: Duplicate key insertion attempted.\n");
            fprintf(outputFile, "Error: Duplicate key insertion attempted.\n");
            // Free the new key/data as they won't be inserted
            if (key && tree->free_key) tree->free_key(key);
            if (data_ptr && tree->free_data) tree->free_data(data_ptr);
            return;
        }
    }
    // If leaf has space
    if (leaf->n < 2 * tree->t - 1) {
        insertIntoLeaf(leaf, key, data_ptr);
    } else { // Leaf is full, need to split
        void *key_to_push_up = NULL; // This will be allocated by splitLeafNode
        BPlusTreeNode *new_leaf = NULL;
        int max_keys = 2 * tree->t -1;
        void **temp_keys = malloc((max_keys + 1) * sizeof(void*));
        void **temp_data = malloc((max_keys + 1) * sizeof(void*));
        if (!temp_keys || !temp_data) {
          //   perror(" Failed to allocate temp arrays for leaf split");
             fprintf(outputFile, " Failed to allocate temp arrays for leaf split. Exiting.\n");
             // Free key/data that couldn't be inserted
             if (key && tree->free_key) tree->free_key(key);
             if (data_ptr && tree->free_data) tree->free_data(data_ptr);
             free(temp_keys); free(temp_data);
             if (outputFile) fclose(outputFile);
          //   exit(EXIT_FAILURE);
        }
        //  merge and split the leaf node
        int i = 0;
        while (i < leaf->n && tree->compare(key, leaf->keys[i]) > 0) i++;
        memcpy(temp_keys, leaf->keys, i * sizeof(void*));
        memcpy(temp_data, leaf->node_type.leaf.data_pointers, i * sizeof(void*));
        temp_keys[i] = key;
        temp_data[i] = data_ptr;
        memcpy(temp_keys + i + 1, leaf->keys + i, (leaf->n - i) * sizeof(void*));
        memcpy(temp_data + i + 1, leaf->node_type.leaf.data_pointers + i, (leaf->n - i) * sizeof(void*));
        splitLeafNode(leaf, &key_to_push_up, &new_leaf);

        free(temp_keys);
        free(temp_data);

        if (!new_leaf || !key_to_push_up) {
           //  fprintf(stderr, "Error: Leaf split failed.\n");
             fprintf(outputFile, "Error: Leaf split failed. Insertion aborted.\n");
             if (key_to_push_up && tree->free_key) tree->free_key(key_to_push_up);
             return;
        }
        // Insert the middle key (copy created in splitLeafNode) into the parent
        insertIntoParent(leaf, key_to_push_up, new_leaf);
    }
}

// Recursive helper to insert into parent, handling splits up the tree
void insertIntoParent(BPlusTreeNode *node, void *key, BPlusTreeNode *right_child) {
    if (!node || !key || !right_child) { 
     //    fprintf(stderr, "Error: Invalid arguments for insertIntoParent.\n");
         fprintf(outputFile, "Error: Invalid arguments for insertIntoParent.\n");
         if (key && node && node->tree && node->tree->free_key) node->tree->free_key(key);
         return;
    }
    BPlusTree *tree = node->tree;
    if (!tree) {
     //    fprintf(stderr, "Error: Node has no tree reference in insertIntoParent.\n");
         fprintf(outputFile, "Error: Node has no tree reference in insertIntoParent.\n");
         if (key && tree && tree->free_key) tree->free_key(key);
         return;
    }
    if (tree->root == node) {
        BPlusTreeNode *new_root = createBPlusTreeNode(tree, false);
        new_root->keys[0] = key;
        new_root->node_type.internal.C[0] = node;
        new_root->node_type.internal.C[1] = right_child;
        new_root->n = 1;
        tree->root = new_root;
        // Key is now owned by the new root, do not free here.
        return;
    }
    //  find parent node
    BPlusTreeNode *parent = NULL;
    BPlusTreeNode *current = tree->root;
    // Simple search assuming 'key' helps find the correct path down
    while (current && !current->is_leaf) {
        int i = 0;
        while (i < current->n && tree->compare(key, current->keys[i]) >= 0) {
            i++;
        }
         bool found_child_match = false;
         if (i <= current->n && current->node_type.internal.C[i] == node) {
             parent = current;
             found_child_match = true;
             break; 
         }
         if (i > 0 && current->node_type.internal.C[i-1] == node) {
             parent = current;
             found_child_match = true;
             break; 
         }

        if (i > current->n || !current->node_type.internal.C[i]) { 
             break; // Reached end or invalid node
        }
        current = current->node_type.internal.C[i]; // Go down
    }

    if (!parent) {
      //   fprintf(stderr, "Critical Error: Could not find parent during insertion split propagation for key.\n");
         fprintf(outputFile, " Error: Could not find parent during insertion split propagation. Key may be lost.\n");
         if (key && tree->free_key) tree->free_key(key);
         return;
    }
    // If parent has space
    if (parent->n < 2 * tree->t - 1) {
        insertIntoInternal(parent, key, right_child);
    } else { // Parent is full, split parent
        void *key_to_push_further_up = NULL; // Pointer from parent node
        BPlusTreeNode *new_internal_node = NULL;
        // Temporarily hold keys and children
        int max_keys = 2 * tree->t - 1;
        int max_children = 2 * tree->t;
        void **temp_keys = malloc((max_keys + 1) * sizeof(void*));
        BPlusTreeNode **temp_C = malloc((max_children + 1) * sizeof(BPlusTreeNode*));
         if (!temp_keys || !temp_C) {
         //    perror("FATAL: Failed to allocate temp arrays for internal split");
             fprintf(outputFile, "Error: Failed to allocate temp arrays for internal split. Exiting.\n");
             if (key && tree->free_key) tree->free_key(key); 
             free(temp_keys); free(temp_C);
             if (outputFile) fclose(outputFile);
          //   exit(EXIT_FAILURE);
        }
        // merge and split internal node 
        int i = 0;
        while (i < parent->n && tree->compare(key, parent->keys[i]) > 0) i++;
        memcpy(temp_keys, parent->keys, i * sizeof(void*));
        memcpy(temp_C, parent->node_type.internal.C, (i + 1) * sizeof(BPlusTreeNode*));
        temp_keys[i] = key; 
        temp_C[i + 1] = right_child;
        memcpy(temp_keys + i + 1, parent->keys + i, (parent->n - i) * sizeof(void*));
        memcpy(temp_C + i + 2, parent->node_type.internal.C + i + 1, (parent->n - i + 1) * sizeof(BPlusTreeNode*)); 
        splitInternalNode(parent, &key_to_push_further_up, &new_internal_node);

        free(temp_keys);
        free(temp_C);

         if (!new_internal_node || !key_to_push_further_up) {
         //    fprintf(stderr, "Error: Internal node split failed.\n");
             fprintf(outputFile, "Error: Internal node split failed. Insertion incomplete.\n");
             return;
         }
        // Recursively insert the middle key into the parent's parent
        insertIntoParent(parent, key_to_push_further_up, new_internal_node);
    }
}


// --- Parking System Logic Implementations ---
void updateMembership(Vehicle *v) {
    if (!v) return;
    MembershipType old_membership = v->membership;
    if (v->total_parking_hours >= 200.0) {
        v->membership = GOLD;
    } else if (v->total_parking_hours >= 100.0) {
        v->membership = PREMIUM;
    } else {
        v->membership = NO_MEMBERSHIP;
    }
}

void loadInitialData(BPlusTree *vehicleTree, BPlusTree *spaceTree) {
    if (!vehicleTree || !spaceTree) {
     //   fprintf(stderr, "Error: Invalid tree pointers passed to loadInitialData.\n");
        fprintf(outputFile, "Error: Invalid tree pointers passed to loadInitialData.\n");
        return;
    }
    FILE *fp = fopen(INPUT_FILENAME, "r");

    // Initialize all spaces first
    fprintf(outputFile, "Initializing %d parking spaces...\n", MAX_SPACES);
    for (int i = 1; i <= MAX_SPACES; ++i) {
        void *space_key = create_space_key(i); // Exits on failure
        if (searchBPlusTree(spaceTree, space_key) == NULL) {
            ParkingSpace *ps = (ParkingSpace*)calloc(1, sizeof(ParkingSpace));
            if (!ps) {
              //  perror("FATAL: Memory allocation failed for space struct during init");
                fprintf(outputFile, "FATAL: Memory allocation failed for space struct %d. Exiting.\n", i);
                if (spaceTree->free_key) spaceTree->free_key(space_key);
                fclose(outputFile);
              //  exit(EXIT_FAILURE);
            }
            ps->space_id = i;
            ps->status = 0; // Free
            ps->occupancy_count = 0;
            ps->total_revenue = 0.0;
            safe_strcpy(ps->parked_vehicle_num, "", sizeof(ps->parked_vehicle_num));
            insertBPlusTree(spaceTree, space_key, ps); // Tree owns key & data now
        } else {
             if (spaceTree->free_key) spaceTree->free_key(space_key); // Free duplicate key
        }
    }
    fprintf(outputFile, "Space initialization complete.\n");


    if (!fp) {
        perror("Error: Could not open initial data file");
        fprintf(outputFile, "Warning: Input data file '%s' not found. Starting with empty vehicle data.\n", INPUT_FILENAME);
        return; // No vehicle data to load
    }

    fprintf(outputFile, "Loading initial data from %s...\n", INPUT_FILENAME);

    char line[512];
    char *token;
    int line_num = 0;
    char *saveptr; 
    // Skip header line
    if (fgets(line, sizeof(line), fp) == NULL) {
        // fprintf(stderr, "Warning: Input file '%s' is empty or contains only header.\n", INPUT_FILENAME);
         fprintf(outputFile, "Warning: Input file '%s' is empty or contains only header.\n", INPUT_FILENAME);
         fclose(fp);
         return;
    }
    line_num++; 
    while (fgets(line, sizeof(line), fp)) {
        line_num++;
        if (line[0] == '\n' || line[0] == '\0') continue;
        line[strcspn(line, "\n")] = 0; // Remove trailing newline
        char *fields[14] = {NULL}; // Initialize to NULL
        int field_count = 0;
        //  assumes no empty fields represented by consecutive tabs
        token = strtok(line, "\t");
        while (token != NULL && field_count < 14) {
            fields[field_count++] = trim_whitespace(token);
            token = strtok(NULL, "\t");
        }

        if (field_count < 14) {
           // fprintf(stderr, "Warning: Skipping line %d in '%s' due to insufficient fields (%d found, expected 14).\n", line_num, INPUT_FILENAME, field_count);
            fprintf(outputFile, "Warning: Skipping line %d due to insufficient fields (%d found).\n", line_num, field_count);
            continue;
        }

        // Extract data with basic validation
        char *vnum_str = fields[0];
        if (!vnum_str || strlen(vnum_str) == 0 || strlen(vnum_str) >= 15) {
            // fprintf(stderr, "Warning: Skipping line %d due to invalid vehicle number '%s'.\n", line_num, vnum_str ? vnum_str : "NULL");
             fprintf(outputFile, "Warning: Skipping line %d due to invalid vehicle number.\n", line_num);
             continue;
        }
        char *owner_str = fields[1] ? fields[1] : "Unknown";
        char *arr_date_str = fields[2];
        char *arr_time_str = fields[3];
        char *arr_ampm_str = fields[4];
        char *dep_date_str = fields[5];
        char *dep_time_str = fields[6];
        char *dep_ampm_str = fields[7];
        char *membership_str = fields[8];
        int space_id = fields[9] ? atoi(fields[9]) : 0;
        int parkings_done = fields[10] ? atoi(fields[10]) : 0;
        double amount_paid = fields[11] ? atof(fields[11]) : 0.0;
        int occupancy = fields[12] ? atoi(fields[12]) : 0; // For the space
        double max_revenue = fields[13] ? atof(fields[13]) : 0.0; // For the space
        void* vehicle_key = create_vehicle_key(vnum_str); // Exits on failure

        Vehicle *v = searchBPlusTree(vehicleTree, vehicle_key);
        bool new_vehicle = (v == NULL);

        if (new_vehicle) {
            v = (Vehicle*)calloc(1, sizeof(Vehicle));
            if (!v) {
            //    perror(" Memory allocation failed for vehicle struct during load");
                fprintf(outputFile, " Memory allocation failed for vehicle struct %s. Exiting.\n", vnum_str);
                if (vehicleTree->free_key) vehicleTree->free_key(vehicle_key);
                fclose(fp); fclose(outputFile);
             //   exit(EXIT_FAILURE);
            }
            safe_strcpy(v->vehicle_number, vnum_str, sizeof(v->vehicle_number));
            insertBPlusTree(vehicleTree, vehicle_key, v); // Tree owns key & data
        } else {
             fprintf(outputFile, "Warning: Vehicle %s found multiple times in file (line %d). Updating data.\n", vnum_str, line_num);
             if (vehicleTree->free_key) vehicleTree->free_key(vehicle_key); // Free duplicate key
        }
        // Update vehicle details
        safe_strcpy(v->owner_name, owner_str, sizeof(v->owner_name));
        v->num_parkings = parkings_done > 0 ? parkings_done : v->num_parkings; // Keep existing if file has 0?
        v->total_amount_paid = amount_paid > 0 ? amount_paid : v->total_amount_paid; // Keep existing if file has 0?

        // Set membership from file string
        if (membership_str) {
            if (strcasecmp(membership_str, "golden") == 0) v->membership = GOLD;
            else if (strcasecmp(membership_str, "premium") == 0) v->membership = PREMIUM;
            else v->membership = NO_MEMBERSHIP;
        } // else keep existing membership if string is NULL

        if (v->total_parking_hours <= 0.1) { 
            if (v->membership == GOLD) v->total_parking_hours = fmax(200.0, v->num_parkings * 2.0);
            else if (v->membership == PREMIUM) v->total_parking_hours = fmax(100.0, v->num_parkings * 2.0);
            else if (v->total_amount_paid > 100) v->total_parking_hours = fmax(1.0, (v->total_amount_paid / 60.0));
            else v->total_parking_hours = fmax(0.0, v->num_parkings * 1.5);
        }
        updateMembership(v); // Re-validate membership based on estimated hours/stats

        bool is_parked_in_file = (dep_date_str && strcmp(dep_date_str, "none") == 0 && space_id > 0 && space_id <= MAX_SPACES);

        if (space_id > 0 && space_id <= MAX_SPACES) {
            void* space_key = create_space_key(space_id); // Exits on failure
            ParkingSpace *ps = searchBPlusTree(spaceTree, space_key);
            if (ps) {
                // Update space stats from file (take the values from the latest line for this space)
                ps->occupancy_count = occupancy >= 0 ? occupancy : ps->occupancy_count;
                ps->total_revenue = max_revenue >= 0 ? max_revenue : ps->total_revenue;

                if (is_parked_in_file) {
                    if (ps->status == 0) {
                        ps->status = 1; // Mark space occupied
                        safe_strcpy(ps->parked_vehicle_num, v->vehicle_number, sizeof(ps->parked_vehicle_num));
                        v->current_parking_space_id = ps->space_id;
                        // Use arrival time from file if valid, else assume NOW
                        v->arrival_time = parseDateTimeString(arr_date_str, arr_time_str, arr_ampm_str);
                        if (v->arrival_time == 0) {
                             fprintf(outputFile, "Warning: Invalid arrival time for parked vehicle %s in file (line %d). Setting arrival to NOW.\n", v->vehicle_number, line_num);
                             v->arrival_time = time(NULL);
                        }
                        v->last_departure_time = 0;
                        char time_buf[30]; formatTime(v->arrival_time, time_buf, sizeof(time_buf));
                        fprintf(outputFile, "Info: Vehicle %s marked as parked in space %d at %s (from file line %d).\n", v->vehicle_number, ps->space_id, time_buf, line_num);
                    } else if (strcmp(ps->parked_vehicle_num, v->vehicle_number) != 0) {
                        // Space occupied, but by a DIFFERENT vehicle according to previous lines/state
                        fprintf(stderr, "Warning: File conflict line %d - Space %d for %s already occupied by %s. Vehicle %s not parked.\n",
                                line_num, space_id, v->vehicle_number, ps->parked_vehicle_num, v->vehicle_number);
                        fprintf(outputFile, "Warning: File conflict line %d - Space %d for %s already occupied by %s. Vehicle %s not parked.\n",
                                line_num, space_id, v->vehicle_number, ps->parked_vehicle_num, v->vehicle_number);
                        v->current_parking_space_id = -1;
                        v->arrival_time = 0;
                    } else {
                         // Space occupied by the SAME vehicle. Update arrival time if file has a valid one.
                         time_t file_arrival = parseDateTimeString(arr_date_str, arr_time_str, arr_ampm_str);
                         if (file_arrival != 0) {
                             v->arrival_time = file_arrival;
                             char time_buf[30]; formatTime(v->arrival_time, time_buf, sizeof(time_buf));
                              fprintf(outputFile, "Info: Updated arrival time for already parked vehicle %s in space %d to %s (from file line %d).\n", v->vehicle_number, ps->space_id, time_buf, line_num);
                         }
                    }
                } else { // Vehicle is NOT parked according to this line
                     // If space *was* marked occupied by *this* vehicle, free it.
                     if (ps->status == 1 && strcmp(ps->parked_vehicle_num, v->vehicle_number) == 0) {
                          fprintf(outputFile, "Info: File line %d indicates %s departed space %d. Marking space free.\n", line_num, v->vehicle_number, ps->space_id);
                          ps->status = 0;
                          safe_strcpy(ps->parked_vehicle_num, "", sizeof(ps->parked_vehicle_num));
                          v->last_departure_time = parseDateTimeString(dep_date_str, dep_time_str, dep_ampm_str);
                          v->current_parking_space_id = -1;
                          v->arrival_time = 0;
                     }
                }
            } else {
               //  fprintf(stderr, "CRITICAL Error: Space %d not found in tree during load (line %d).\n", space_id, line_num);
                 fprintf(outputFile, "CRITICAL Error: Space %d not found in tree during load (line %d).\n", space_id, line_num);
            }
             if (spaceTree->free_key) spaceTree->free_key(space_key); // Free search key
        } else if (is_parked_in_file) {
          //   fprintf(stderr, "Error: File line %d indicates vehicle %s parked in invalid space %d. Marking as not parked.\n", line_num, v->vehicle_number, space_id);
             fprintf(outputFile, "Error: File line %d indicates vehicle %s parked in invalid space %d. Marking as not parked.\n", line_num, v->vehicle_number, space_id);
             is_parked_in_file = false; 
        }

        if (!is_parked_in_file && v->current_parking_space_id != -1) {
             if (v->current_parking_space_id > 0) { 
                 fprintf(outputFile, "Info: Correcting state for vehicle %s - marking as not parked based on file line %d.\n", v->vehicle_number, line_num);
             }
             v->current_parking_space_id = -1;
             v->arrival_time = 0;
             // Set last departure time if available and not currently parked
             if (dep_date_str && strcmp(dep_date_str, "none") != 0) {
                  v->last_departure_time = parseDateTimeString(dep_date_str, dep_time_str, dep_ampm_str);
             }
        }
    } 
    fclose(fp);
    fprintf(outputFile, "Initial data loading complete.\n");
}


// Helper to find the first available space in a range by iterating through linked leaves
int findSpaceInRangeFromLeaves(BPlusTree *spaceTree, int start_id, int end_id) {
    if (!spaceTree || !spaceTree->first_leaf) return -1; // No tree or no leaves

    BPlusTreeNode *current_leaf = spaceTree->first_leaf;
    while (current_leaf != NULL) {
        if (!current_leaf->is_leaf || !current_leaf->keys || !current_leaf->node_type.leaf.data_pointers) {
          //   fprintf(stderr, "Error: Corrupted leaf node encountered during space search.\n");
             fprintf(outputFile, "Error: Corrupted leaf node encountered during space search.\n");
             current_leaf = current_leaf->node_type.leaf.next; // Try next leaf
             continue;
        }

        for (int i = 0; i < current_leaf->n; i++) {
            if (!current_leaf->keys[i]) continue;
            int current_space_id = *(int*)current_leaf->keys[i];

            if (current_space_id >= start_id && current_space_id <= end_id) {
                ParkingSpace *ps = (ParkingSpace*)current_leaf->node_type.leaf.data_pointers[i];
                if (ps && ps->status == 0) {
                    return ps->space_id; 
                }
            }
            //  If current key is already past the end_id, we can stop.
            if (current_space_id > end_id) {
                 return -1; 
            }
        }
        current_leaf = current_leaf->node_type.leaf.next; 
    }
    return -1; 
}


int findAvailableSpace(BPlusTree *spaceTree, MembershipType membership) {
    int space_id = -1;

    // Try preferred range first using leaf traversal
    if (membership == GOLD) {
        space_id = findSpaceInRangeFromLeaves(spaceTree, 1, MAX_SPACES);
        fprintf(outputFile, "Searching for GOLD space , Found: %d\n", space_id);
    }
    if (space_id == -1 && (membership == GOLD || membership == PREMIUM)) {
         space_id = findSpaceInRangeFromLeaves(spaceTree, 11, MAX_SPACES);
         fprintf(outputFile, "Searching for PREMIUM space , Found: %d\n", space_id);
    }
    if (space_id == -1) {
         space_id = findSpaceInRangeFromLeaves(spaceTree, 21, MAX_SPACES);
         fprintf(outputFile, "Searching for GENERAL space (21-%d)... Found: %d\n", MAX_SPACES, space_id);
    }
    return space_id;
}

void handleVehicleEntry(BPlusTree *vehicleTree, BPlusTree *spaceTree) {
    char vehicle_num[15];
    fprintf(outputFile, "\n--- Vehicle Entry ---\n");
    printf("Enter Vehicle Number: "); // Prompt on console
    if (scanf("%14s", vehicle_num) != 1) {
        fprintf(stderr, "Error reading vehicle number.\n");
        clearInputBuffer();
        fprintf(outputFile, "Error: Invalid vehicle number input.\n");
        fprintf(outputFile, "--- Vehicle Entry End ---\n");
        return;
    }
    clearInputBuffer();
    void* vehicle_key = create_vehicle_key(vehicle_num); // Exits on failure

    Vehicle *v = searchBPlusTree(vehicleTree, vehicle_key);

    if (v) { // Existing vehicle
        if (vehicleTree->free_key) vehicleTree->free_key(vehicle_key); // Free search key, not needed anymore

        if (v->current_parking_space_id != -1) {
            fprintf(outputFile, "Error: Vehicle %s is already parked in space %d.\n", vehicle_num, v->current_parking_space_id);
            fprintf(outputFile, "--- Vehicle Entry End ---\n");
            return;
        }
        fprintf(outputFile, "Welcome back, %s (%s Membership)!\n", v->owner_name, membership_strings[v->membership]);
        int space_id = findAvailableSpace(spaceTree, v->membership);

        if (space_id == -1) {
            fprintf(outputFile, "Sorry, no suitable parking space available at the moment.\n");
            return;
        }

        void* space_key = create_space_key(space_id); 
        ParkingSpace *ps = searchBPlusTree(spaceTree, space_key);
        if (spaceTree->free_key) spaceTree->free_key(space_key); // Free search key

        if (ps && ps->status == 0) {
            ps->status = 1; // Occupy space
            safe_strcpy(ps->parked_vehicle_num, v->vehicle_number, sizeof(ps->parked_vehicle_num));
            v->current_parking_space_id = space_id;
            v->arrival_time = time(NULL); // Use current time for arrival
            v->last_departure_time = 0; // Clear last departure time

            char time_buf[30];
            formatTime(v->arrival_time, time_buf, sizeof(time_buf));
            fprintf(outputFile, "Vehicle %s parked in space %d at %s.\n", v->vehicle_number, space_id, time_buf);
        } else {
             fprintf(outputFile, "Error: Could not allocate space %d. Status: %s. Race condition or logic error?\n",
                    space_id, ps ? (ps->status ? "Occupied" : "Free") : "Not Found");
             fprintf(outputFile, "Parking allocation failed. Please try again.\n");
        }

    } else { // New vehicle
        // vehicle_key is already created and valid (ownership passed to tree on insert)
        fprintf(outputFile, "Registering new vehicle: %s\n", vehicle_num);
        char owner_name[50];
        char arrival_time_str[30];
        time_t arrival_time_input = 0;

        printf("Enter Owner Name: "); // Prompt on console
        if (fgets(owner_name, sizeof(owner_name), stdin)) {
             owner_name[strcspn(owner_name, "\n")] = 0; // Remove trailing newline
             if (strlen(owner_name) == 0) strcpy(owner_name, "Unknown"); // Handle empty input
        } else {
            fprintf(stderr, "Error reading owner name. Using 'Unknown'.\n");
            clearInputBuffer();
            strcpy(owner_name, "Unknown");
        }
        fprintf(outputFile, "Owner Name: %s\n", owner_name);

        // Get arrival time from user
        while (arrival_time_input == 0) {
            printf("Enter Arrival Time (YYYY-MM-DD HH:MM:SS): "); // Prompt on console
            if (fgets(arrival_time_str, sizeof(arrival_time_str), stdin)) {
                 arrival_time_str[strcspn(arrival_time_str, "\n")] = 0;
                 arrival_time_input = parseUserInputDateTime(arrival_time_str); // Uses sscanf now
                 if (arrival_time_input == 0) {
                     printf("Invalid format or date/time. Please try again.\n"); // Console feedback
                 }
            } else {
                 fprintf(stderr, "Error reading arrival time input stream.\n");
                 clearInputBuffer();
                 fprintf(outputFile, "Error reading arrival time.\n");
                 if (vehicleTree->free_key) vehicleTree->free_key(vehicle_key); // Free key if aborting
                 fprintf(outputFile, "--- Vehicle Entry End ---\n");
                 return;
            }
        }
         char time_buf_in[30];
         formatTime(arrival_time_input, time_buf_in, sizeof(time_buf_in));
         fprintf(outputFile, "Arrival Time Entered: %s\n", time_buf_in);

        // New vehicles get non-member allocation policy
        int space_id = findAvailableSpace(spaceTree, NO_MEMBERSHIP);

        if (space_id == -1) {
            fprintf(outputFile, "Sorry, no parking space available for new vehicles at the moment.\n");
            if (vehicleTree->free_key) vehicleTree->free_key(vehicle_key); // Free key if no space
            fprintf(outputFile, "--- Vehicle Entry End ---\n");
            return;
        }

        void* space_key = create_space_key(space_id); 
        ParkingSpace *ps = searchBPlusTree(spaceTree, space_key);
        if (spaceTree->free_key) spaceTree->free_key(space_key); // Free search key

        if (ps && ps->status == 0) {
            Vehicle *new_v = (Vehicle*)calloc(1, sizeof(Vehicle));
            if (!new_v) {
            //    perror(" Failed to allocate memory for new vehicle struct");
                fprintf(outputFile, " Failed to allocate memory for new vehicle struct %s. Exiting.\n", vehicle_num);
                if (vehicleTree->free_key) vehicleTree->free_key(vehicle_key); // Free key
                fclose(outputFile);
            //    exit(EXIT_FAILURE);
            }

            safe_strcpy(new_v->vehicle_number, vehicle_num, sizeof(new_v->vehicle_number));
            safe_strcpy(new_v->owner_name, owner_name, sizeof(new_v->owner_name));
            new_v->arrival_time = arrival_time_input; // Use user-provided time
            new_v->last_departure_time = 0;
            new_v->membership = NO_MEMBERSHIP;
            new_v->total_parking_hours = 0.0;
            new_v->num_parkings = 0; // Will be incremented on first exit
            new_v->total_amount_paid = 0.0;
            new_v->current_parking_space_id = space_id;

            ps->status = 1; 
            safe_strcpy(ps->parked_vehicle_num, new_v->vehicle_number, sizeof(ps->parked_vehicle_num));

            // Insert the new vehicle (key already created)
            insertBPlusTree(vehicleTree, vehicle_key, new_v);
            // Tree now owns vehicle_key and new_v

            fprintf(outputFile, "Vehicle %s registered and parked in space %d at %s.\n", new_v->vehicle_number, space_id, time_buf_in);

        } else {
             fprintf(outputFile, "Error: Could not allocate space %d for new vehicle. Status: %s.\n",
                    space_id, ps ? (ps->status ? "Occupied" : "Free") : "Not Found");
             fprintf(outputFile, "Parking allocation failed. Please try again.\n");
             if (vehicleTree->free_key) vehicleTree->free_key(vehicle_key); // Free key if allocation failed
        }
    }
     fprintf(outputFile, "--- Vehicle Entry End ---\n");
}


double calculateParkingFee(double hours, MembershipType membership) {
    if (hours < 0) hours = 0; 
    double fee;
    if (hours <= 3.0) {
        fee = 100.0;
    } else {
        double extra_hours = hours - 3.0;
        // Charge 50 for each hour or part thereof using ceil
        fee = 100.0 + ceil(extra_hours) * 50.0;
    }
    if (membership == PREMIUM || membership == GOLD) {
        fee *= 0.90; // 10% discount
    }
    return fee;
}


void handleVehicleExit(BPlusTree *vehicleTree, BPlusTree *spaceTree) {
    char vehicle_num[15];
     fprintf(outputFile, "\n--- Vehicle Exit ---\n");
    printf("Enter Vehicle Number to Exit: "); // Prompt on console
     if (scanf("%14s", vehicle_num) != 1) {
     //   fprintf(stderr, "Error reading vehicle number.\n");
        clearInputBuffer();
        fprintf(outputFile, "Error: Invalid vehicle number input.\n");
        fprintf(outputFile, "--- Vehicle Exit End ---\n");
        return;
    }
    clearInputBuffer();
    fprintf(outputFile, "Processing exit for: %s\n", vehicle_num);

    void* vehicle_key = create_vehicle_key(vehicle_num); 
    Vehicle *v = searchBPlusTree(vehicleTree, vehicle_key);
    if (vehicleTree->free_key) vehicleTree->free_key(vehicle_key); // Free search key

    if (!v) {
        fprintf(outputFile, "Error: Vehicle %s not found in the system.\n", vehicle_num);
         fprintf(outputFile, "--- Vehicle Exit End ---\n");
        return;
    }

    if (v->current_parking_space_id == -1 || v->arrival_time == 0) {
        fprintf(outputFile, "Error: Vehicle %s is not currently parked.\n", vehicle_num);
         fprintf(outputFile, "--- Vehicle Exit End ---\n");
        return;
    }

    time_t departure_time = time(NULL); // Use current system time for departure
    time_t original_arrival = v->arrival_time; 
    double duration_seconds = difftime(departure_time, original_arrival);
    if (duration_seconds < 0) duration_seconds = 0; // Handle clock skew

    double duration_hours = duration_seconds / 3600.0;
    MembershipType old_membership = v->membership;
    v->total_parking_hours += duration_hours;
    v->num_parkings++;
    v->last_departure_time = departure_time;

    updateMembership(v); // Update membership based on new total hours

    double fee = calculateParkingFee(duration_hours, v->membership); 
    v->total_amount_paid += fee;

    int space_id = v->current_parking_space_id;
    // Update vehicle state *before* updating space
    v->current_parking_space_id = -1;
    v->arrival_time = 0; // Mark as not parked

    // Update Parking Space
    void* space_key = create_space_key(space_id); 
    ParkingSpace *ps = searchBPlusTree(spaceTree, space_key);
    if (ps) {
        ps->status = 0; // Free the space
        ps->occupancy_count++;
        ps->total_revenue += fee;
        safe_strcpy(ps->parked_vehicle_num, "", sizeof(ps->parked_vehicle_num)); // Clear parked vehicle
    } else {
    //    fprintf(stderr, "CRITICAL Error: Parking space %d data not found for exiting vehicle %s!\n", space_id, vehicle_num);
        fprintf(outputFile, "CRITICAL Error: Space %d data missing during exit of %s!\n", space_id, vehicle_num);
    }
    if (spaceTree->free_key) spaceTree->free_key(space_key); // Free search key

    // --- Print Receipt to output file ---
    char time_buf_dep[30], time_buf_arr_orig[30];
    formatTime(departure_time, time_buf_dep, sizeof(time_buf_dep));
    formatTime(original_arrival, time_buf_arr_orig, sizeof(time_buf_arr_orig)); // Use stored arrival time

    fprintf(outputFile, "\n--- Vehicle Exit Receipt ---\n");
    fprintf(outputFile, "Vehicle Number: %s\n", v->vehicle_number);
    fprintf(outputFile, "Owner Name: %s\n", v->owner_name);
    fprintf(outputFile, "Arrival Time: %s\n", time_buf_arr_orig);
    fprintf(outputFile, "Departure Time: %s\n", time_buf_dep);
    fprintf(outputFile, "Duration Parked: %.2f hours\n", duration_hours);
    fprintf(outputFile, "Current Fee: %.2f Rs\n", fee);
    if (v->membership != old_membership) {
         fprintf(outputFile, "Membership Status Updated: %s -> %s\n", membership_strings[old_membership], membership_strings[v->membership]);
    } else {
         fprintf(outputFile, "Membership Status: %s\n", membership_strings[v->membership]);
    }
    if (v->membership == PREMIUM || v->membership == GOLD) {
        fprintf(outputFile, "Discount Applied: 10%%\n");
    }
    fprintf(outputFile, "Total Hours Parked (All Time): %.2f\n", v->total_parking_hours);
    fprintf(outputFile, "Total Amount Paid (All Time): %.2f\n", v->total_amount_paid);
    fprintf(outputFile, "Total Parkings: %d\n", v->num_parkings);
    fprintf(outputFile, "Space %d is now free.\n", space_id);
    fprintf(outputFile, "----------------------------\n");
    fprintf(outputFile, "--- Vehicle Exit End ---\n");

}

// Writes vehicle details to the global outputFile
void displayVehicleDetails(const Vehicle *v) {
    if (!v) return;
    char arrival_buf[30], departure_buf[30];
    formatTime(v->arrival_time, arrival_buf, sizeof(arrival_buf));
    formatTime(v->last_departure_time, departure_buf, sizeof(departure_buf));

    fprintf(outputFile, " VNum: %-14s | Owner: %-20s | Mem: %-7s | Total Hrs: %7.2f | Parkings: %3d | Paid: %8.2f | Parked in: %-3d | Arrived: %s | Last Left: %s\n",
           v->vehicle_number,
           v->owner_name ? v->owner_name : "N/A", // Handle potential NULL owner name
           membership_strings[v->membership],
           v->total_parking_hours,
           v->num_parkings,
           v->total_amount_paid,
           v->current_parking_space_id == -1 ? 0 : v->current_parking_space_id, // Display 0 or space ID
           arrival_buf,
           departure_buf);
}

// Writes space details to the global outputFile
void displaySpaceDetails(const ParkingSpace *ps) {
     if (!ps) return;
     fprintf(outputFile, " Space ID: %-3d | Status: %-8s | Occupancy Count: %-5d | Total Revenue: %8.2f | Parked VNum: %s\n",
            ps->space_id,
            ps->status == 0 ? "Free" : "Occupied",
            ps->occupancy_count,
            ps->total_revenue,
            ps->status == 1 ? (ps->parked_vehicle_num[0] != '\0' ? ps->parked_vehicle_num : "UNKNOWN") : "---");
}

// --- Reporting List Helper Function Implementations ---

// Add vehicle to sorted list by parkings (desc)
ReportVehicleNode* insertSortedVehicleByParkings(ReportVehicleNode *head, Vehicle *v) {
    if (!v) return head; 
    ReportVehicleNode *newNode = (ReportVehicleNode*)malloc(sizeof(ReportVehicleNode));
    if (!newNode) { perror("Failed to allocate report node"); return head;} 
    newNode->vehicle = v; newNode->next = NULL;
    if (!head || v->num_parkings > head->vehicle->num_parkings) {
        newNode->next = head; return newNode;
    }
    ReportVehicleNode *curr = head;
    while (curr->next && v->num_parkings <= curr->next->vehicle->num_parkings) curr = curr->next;
    newNode->next = curr->next; curr->next = newNode;
    return head;
}

// Add vehicle to sorted list by amount paid (desc)
ReportVehicleNode* insertSortedVehicleByAmount(ReportVehicleNode *head, Vehicle *v) {
     if (!v) return head;
    ReportVehicleNode *newNode = (ReportVehicleNode*)malloc(sizeof(ReportVehicleNode));
     if (!newNode) { perror("Failed to allocate report node"); return head;}
    newNode->vehicle = v; newNode->next = NULL;
    if (!head || v->total_amount_paid > head->vehicle->total_amount_paid) {
        newNode->next = head; return newNode;
    }
    ReportVehicleNode *curr = head;
    while (curr->next && v->total_amount_paid <= curr->next->vehicle->total_amount_paid) curr = curr->next;
    newNode->next = curr->next; curr->next = newNode;
    return head;
}

// Add space to sorted list by occupancy (desc)
ReportSpaceNode* insertSortedSpaceByOccupancy(ReportSpaceNode *head, ParkingSpace *ps) {
    if (!ps) return head;
    ReportSpaceNode *newNode = (ReportSpaceNode*)malloc(sizeof(ReportSpaceNode));
    if (!newNode) { perror("Failed to allocate report node"); return head;}
    newNode->space = ps; newNode->next = NULL;
    if (!head || ps->occupancy_count > head->space->occupancy_count) {
        newNode->next = head; return newNode;
    }
    ReportSpaceNode *curr = head;
    while (curr->next && ps->occupancy_count <= curr->next->space->occupancy_count) curr = curr->next;
    newNode->next = curr->next; curr->next = newNode;
    return head;
}

// Add space to sorted list by revenue (desc)
ReportSpaceNode* insertSortedSpaceByRevenue(ReportSpaceNode *head, ParkingSpace *ps) {
     if (!ps) return head;
     ReportSpaceNode *newNode = (ReportSpaceNode*)malloc(sizeof(ReportSpaceNode));
     if (!newNode) { perror("Failed to allocate report node"); return head;}
    newNode->space = ps; newNode->next = NULL;
    if (!head || ps->total_revenue > head->space->total_revenue) {
        newNode->next = head; return newNode;
    }
    ReportSpaceNode *curr = head;
    while (curr->next && ps->total_revenue <= curr->next->space->total_revenue) curr = curr->next;
    newNode->next = curr->next; curr->next = newNode;
    return head;
}

// Traverses B+ Tree leaves and builds sorted list
void collectVehiclesSorted(BPlusTree *tree, ReportVehicleNode **listHead, int sortType) {
    *listHead = NULL;
    if (!tree || !tree->first_leaf) return; 
    BPlusTreeNode *current_leaf = tree->first_leaf;
    while (current_leaf != NULL) {
         if (!current_leaf->is_leaf || !current_leaf->node_type.leaf.data_pointers) {
        //     fprintf(stderr, "Error: Corrupted leaf node during vehicle collection.\n");
             fprintf(outputFile, "Error: Corrupted leaf node during vehicle collection.\n");
             current_leaf = current_leaf->node_type.leaf.next; // Try next
             continue;
         }
        for (int i = 0; i < current_leaf->n; i++) {
            Vehicle *v = (Vehicle*)current_leaf->node_type.leaf.data_pointers[i];
            if (v) { 
                 if (sortType == 1) *listHead = insertSortedVehicleByParkings(*listHead, v);
                 else if (sortType == 2) *listHead = insertSortedVehicleByAmount(*listHead, v);
                 else { // Unsorted (append)
                    ReportVehicleNode *newNode = (ReportVehicleNode*)malloc(sizeof(ReportVehicleNode));
                    if(newNode){
                        newNode->vehicle = v; newNode->next = NULL;
                        if(!*listHead) *listHead = newNode;
                        else { ReportVehicleNode *temp = *listHead; while(temp->next) temp=temp->next; temp->next = newNode; }
                    } else {
                         perror("Failed to allocate report node for unsorted list");
                    }
                 }
            }
        }
        current_leaf = current_leaf->node_type.leaf.next;
    }
}

// Traverses B+ Tree leaves and builds sorted list
void collectSpacesSorted(BPlusTree *tree, ReportSpaceNode **listHead, int sortType) {
     *listHead = NULL;
     if (!tree || !tree->first_leaf) return;
    BPlusTreeNode *current_leaf = tree->first_leaf;
    while (current_leaf != NULL) {
         if (!current_leaf->is_leaf || !current_leaf->node_type.leaf.data_pointers) {
          //   fprintf(stderr, "Error: Corrupted leaf node during space collection.\n");
             fprintf(outputFile, "Error: Corrupted leaf node during space collection.\n");
             current_leaf = current_leaf->node_type.leaf.next; // Try next
             continue;
         }
        for (int i = 0; i < current_leaf->n; i++) {
            ParkingSpace *ps = (ParkingSpace*)current_leaf->node_type.leaf.data_pointers[i];
             if (ps) { 
                 if (sortType == 1) *listHead = insertSortedSpaceByOccupancy(*listHead, ps);
                 else if (sortType == 2) *listHead = insertSortedSpaceByRevenue(*listHead, ps);
                 else { 
                     ReportSpaceNode *newNode = (ReportSpaceNode*)malloc(sizeof(ReportSpaceNode));
                     if(newNode){
                         newNode->space = ps; newNode->next = NULL;
                         if(!*listHead) *listHead = newNode;
                         else { ReportSpaceNode *temp = *listHead; while(temp->next) temp=temp->next; temp->next = newNode; }
                     } else {
                          perror("Failed to allocate report node for unsorted list");
                     }
                 }
            }
        }
        current_leaf = current_leaf->node_type.leaf.next;
    }
}

// Free report list nodes (NOT the data they point to)
void freeReportVehicleList(ReportVehicleNode *head) {
    ReportVehicleNode *tmp;
    while (head != NULL) { tmp = head; head = head->next; free(tmp); }
}
void freeReportSpaceList(ReportSpaceNode *head) {
     ReportSpaceNode *tmp;
    while (head != NULL) { tmp = head; head = head->next; free(tmp); }
}

// --- Memory Management ---
void destroyBPlusTreeNodeRecursive(BPlusTreeNode *node) {
    if (!node) return;
    BPlusTree *tree = node->tree; 
    if (!tree) {
        fprintf(stderr, "Warning: Destroying node with no tree reference.\n");
        free(node->keys); // Assuming keys were allocated
        free(node);
        return;
    }
    if (node->is_leaf) {
        // Free keys and data pointers in the leaf
        if (node->keys) {
            for (int i = 0; i < node->n; i++) {
                if (node->keys[i] && tree->free_key) {
                    tree->free_key(node->keys[i]);
                }
            }
            free(node->keys);
        }
        if (node->node_type.leaf.data_pointers) {
             for (int i = 0; i < node->n; i++) {
                 if (node->node_type.leaf.data_pointers[i] && tree->free_data) {
                    tree->free_data(node->node_type.leaf.data_pointers[i]);
                }
             }
            free(node->node_type.leaf.data_pointers);
        }
    } else { // Internal node
        // Free keys
         if (node->keys) {
            for (int i = 0; i < node->n; i++) {
                if (node->keys[i] && tree->free_key) {
                    tree->free_key(node->keys[i]);
                }
            }
            free(node->keys);
         }
        // Recursively destroy children
        if (node->node_type.internal.C) {
            // Destroy all potential children (n+1 pointers)
            for (int i = 0; i <= node->n; i++) { 
                 if (node->node_type.internal.C[i]) { 
                    destroyBPlusTreeNodeRecursive(node->node_type.internal.C[i]);
                 }
            }
             // Check remaining pointers up to max capacity (2*t) - should be NULL if n is correct
             for (int i = node->n + 1; i < 2 * tree->t; ++i) {
                 if (node->node_type.internal.C[i]) {
                      fprintf(stderr, "Error: Found non-NULL child pointer beyond node->n during destruction.\n");
                      destroyBPlusTreeNodeRecursive(node->node_type.internal.C[i]);
                 }
             }
            free(node->node_type.internal.C);
        }
    }
    // Free the node itself
    free(node);
}

void destroyBPlusTree(BPlusTree *tree) {
    if (tree) {
        if (tree->root) {
            destroyBPlusTreeNodeRecursive(tree->root);
        }
        free(tree); // Free the tree structure itself
    }
}

