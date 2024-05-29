# CIS3050 Assignment 3 - Thread spell-checker

Implementing a spell-checker for the C- language. The program makes use of threads for concurrent execution of tasks and running computationally intensive code in parellel using multi-core CPUs.


## Usage

1. To compile the program spellchecker.c, type **make** or **make A3checker**.
1. An executable called **A3checker** will be created; it can be run using **./A3checker**.
1. To delete the executable, type **make clean**.


## Implementation Details

When the program starts, the main menu will be displayed to the user, allowing them to choose to start a spellchecking task or exit the program. Upon choosing to start a spellchecking task, a sub-menu is displayed. The sub-menu asks the user for the text file to spellcheck and the dictionary file to check it against. Alternatively, the user can input the word "back" on the prompts when asked for the file names to return to the main menu, cancelling the current spellchecking task. Receiving user input in the main menu is done in a separate thread from the main program to allow finished spellchecking tasks to interrupt and output their results. The threading function for this is threadInput().

In terms of accepted punctuation, I've allowed apostrophes as those were found quite often in the example american-english dictionary provided. Additionally, i've allowed hypens as hyphenated words are not uncommon in the english language. All other punctuation attache to words have been trimmed - the same goes for all characters not reasonably found in words, such as numbers. All strings were converted to lower case as spellchecking will be case insensitive. Max word size is assumed to be 70 since the there are no english words above 50 characters. I did not only count unique incorrect words towards mistakes but also their reoccurrences - if an incorrect word occurred 50 times then it contributes 50 counts to the total number of mistakes.

I mimicked the msgqueueCondition.c example and created a queue to hold the output for finished spellchecking tasks - the OutputQueue struct. The createOutputQueue, insertNode, and deleteNode functions help me to interface with the OutputQueue. Each time the user starts a new spellchecking task, a new thread is created - the threading function for this is threadFunction(). The function reads the given files and performs the spellchecking, creating a dictionary struct and an array of InputWords structs to help organize the data. If the spellchecking thread is terminated abruptly, then the cleanupHandler() function frees up the memory allocated in threadFunction(). When threadFunction() is done, it puts the output in the OutputQueue to be displayed when the user is idling on the main menu. The output holds control of the program until the user enters any input to acknowledge the output, then the program continues with the user being able to begin a new task or exit.


## Limitations
The main menu asking if the user wants to start a new spellchecking task or exit displays twice.
The 5 most common incorrect words are not properly ordered.
The Levenshtein distance calculations were not done in spellchecking.