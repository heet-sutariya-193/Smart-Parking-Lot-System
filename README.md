# Smart Car Parking System

This repository contains a C implementation of a Smart Car Parking System, designed to manage vehicle parking and space occupancy efficiently. The system leverages B+ Tree data structures for optimized storage and retrieval of vehicle and parking space information, enabling fast lookups and reporting functionalities.

## Features

The system offers the following functionalities:

* **Vehicle Management:**
    * Vehicle Entry: Records new vehicle arrivals and assigns parking spaces.
    * Vehicle Exit: Processes vehicle departures, calculates parking fees, and updates statistics.
    * Membership Handling: Supports `Premium`, `Gold`, and `No Membership` tiers, potentially affecting parking fees.
* **Parking Space Management:**
    * Assigns and tracks the status (free/occupied) of up to 50 parking spaces.
* **Data Persistence:**
    * Loads initial vehicle and parking space data from `file.txt`.
    * Generates comprehensive reports and logs system activities to `output.txt`.
* **Reporting:**
    * Prints vehicles sorted by total parking count.
    * Prints vehicles within a specified range of total amount paid.
    * Prints parking spaces sorted by occupancy count.
    * Prints parking spaces sorted by total revenue generated.
    * Displays all vehicle and parking space details.

## Concepts Used

### B+ Tree Data Structure

The core of this parking system relies on two B+ Trees:
1.  **`vehicleTree`**: Stores `Vehicle` records, keyed by `vehicle_number` (a string). This allows for efficient searching, insertion, and retrieval of vehicle details.
2.  **`spaceTree`**: Manages `ParkingSpace` records, keyed by `space_id` (an integer). This facilitates quick assignment of available spaces and updates to their status and revenue.

**Why B+ Trees?**
B+ Trees are particularly well-suited for this application due to their efficient performance in:
* **Range Queries:** All data is stored in leaf nodes, which are linked together, allowing for efficient traversal and range-based reporting (e.g., vehicles by amount paid range, spaces by revenue).
* **Disk-Based Storage (Conceptual):** While this is an in-memory implementation, B+ Trees are optimized for disk I/O, which is beneficial for large datasets where data might conceptually reside on disk.
* **Ordered Traversal:** Leaf nodes form a sorted linked list, enabling easy iteration over all stored records for reports that require sorted output.
* **Efficient Inserts and Searches:** The tree structure ensures logarithmic time complexity for search and insert operations.

### File Handling

The system interacts with two external text files:

* **`file.txt` (Input File):**
    * This file serves as the initial data source for the parking system. It contains records of vehicles and parking spaces, including details like vehicle number, owner name, arrival/departure times, membership type, and initial parking statistics.
    * The `loadInitialData` function reads and parses this file to populate the B+ Trees at the start of the program. The format of `file.txt` is expected to be tab-separated values, as indicated by the `trim_whitespace` function and parsing logic.
    * **Example format (first line is header, subsequent lines are data):**
        ```
        Vehicle_Number	Owner_Name	Arr_Date	Arr_Time	Arr_AMPM	Dep_Date	Dep_Time	Dep_AMPM	Membership	Space_ID	Parkings_Done	Amount_Paid	Occupancy	Max_Revenue
        PQR789	Bob	20-04-2024	09:15:00	AM	21-04-2024	06:20:00	PM	none	25	1	500	1	500
        LMN321	Charlie	05-05-2024	12:00:00	PM	none	none	none	none	30	1	0	1	0
        ```
* **`output.txt` (Output/Log File):**
    * All significant program outputs, including system initialization messages, user interaction logs, vehicle details, parking space details, and generated reports, are written to this file.
    * The `outputFile` global pointer is used to direct `fprintf` calls to this file throughout the program's execution.
    * This provides a persistent record of the system's operations and generated reports.

## How to Compile and Run

### Prerequisites

You need a C compiler installed on your system (e.g., GCC for Linux/Windows, Clang for macOS).

### Steps

1.  **Clone or Download the Repository:**
    ```bash
    git clone [https://github.com/your-username/smart-car-parking-system.git](https://github.com/your-username/smart-car-parking-system.git)
    cd smart-car-parking-system
    ```
    (Replace `your-username/smart-car-parking-system.git` with your actual repository URL and `smart-car-parking-system` with your chosen repository name.)

2.  **Ensure `smart_parking_system.c` and `file.txt` are Present:**
    * Make sure the C source file (`smart_parking_system.c`) and the input data file (`file.txt`) are located in the same directory.

3.  **Compile the Code:**
    * Open your terminal or command prompt.
    * Navigate to the directory where you cloned/downloaded the project.
    * Compile the C file using a command like GCC:
        ```bash
        gcc smart_parking_system.c -o parking_system -lm
        ```
        * `-o parking_system`: Specifies the output executable name as `parking_system`.
        * `-lm`: Links the math library, necessary for functions like `ceil` and `fmax` used in the code.

4.  **Run the Executable:**
    * Execute the compiled program from your terminal:
        ```bash
        ./parking_system
        ```
    * On Windows, you might need to type `parking_system.exe`.

5.  **Interact with the System:**
    * The program will display a menu in your terminal.
    * Follow the prompts to interact with the parking system (e.g., enter vehicles, exit vehicles, generate reports).
    * All reports and significant system messages will be written to `output.txt` in the same directory.
