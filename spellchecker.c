/*
    Peter Woon-Fat | pwoonfat@uoguelph.ca | 1048220
    CIS3050 Assignment 3
    spellchecker.c
*/

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/time.h>
#include <ctype.h>
#include <stdbool.h>

// structs needed for hash table storing dictionary words
// each bucket stores all words starting with certain letter of alphabet
typedef struct {
    int size;
    char **words;
} bucket;

typedef struct {
    bucket buckets[26];
} dictionary;

// struct keeps track of words read from input file
typedef struct {
    char word[75];
    int count;
} InputWord;

// struct tracks all data to output from spellchecking task
typedef struct {
    char input_file[70];
    char dict_file[70];
    int mistakes;
    InputWord incorrectWords[5];
} SpellcheckOutput;

// node struct to hold all data used in spellchecking
// each file will have an associated node
typedef struct output_node {
    SpellcheckOutput output;
    struct output_node *next;
} OutputNode;

typedef struct {
    OutputNode *head;
    OutputNode *tail;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} OutputQueue;

//Each thread needs multiple arguments, so we create a dedicated struct
typedef struct {
    int threadId;
    char input_file[100];
    char dict_file[100];
    OutputQueue *outputQueue;
} threadArgs;

typedef struct {
    int *option;
} threadInputArgs;

typedef struct {
    dictionary *dict;
    InputWord *inputWords;
} CleanupArgs;

void *threadFunction(void *arg);
void *threadInput(void *arg);
int mainMenu();
int subMenu(char *input, char *dict);
OutputQueue *createOutputQueue();
void insertNode(OutputQueue *outputQueue, int mistakes, InputWord incorrectWords[5], char input_file[70], char dict_file[70]);
int deleteNode(OutputQueue *outputQueue, SpellcheckOutput *output);
void formatString(char *str);
void getBucketSizes(int *counts, char word[70]);
void sortWord(dictionary *dict, char word[70], int *indexes);
int getBucket(char word[70]);
void orderTopMistakes(SpellcheckOutput *output, InputWord inputWord);
void cleanupHandler(void *arg);

int main() {
    // keep track of threads using array
    pthread_t *tid = malloc(0);
    threadArgs *args = malloc(0);
    OutputQueue *outputQueue = createOutputQueue();
    int running_threads = 0;
    char input_file[100];
    char dict_file[100];
    // loop main menu until user wants to exit
    while (1) {
        int mainMenuOption = mainMenu();
        if (mainMenuOption == 1) {
            if (subMenu(input_file, dict_file) == 0) {
                printf("\n");
                continue;
            } else {
                // initiate spellchecking task in another thread
                // need to lock when modifying outputQueue
                running_threads += 1;
                tid = realloc(tid, sizeof(pthread_t) * running_threads);
                args = realloc(args, sizeof(threadArgs) * running_threads);
                args[running_threads - 1].threadId = running_threads;
                strcpy(args[running_threads - 1].input_file, input_file);
                strcpy(args[running_threads - 1].dict_file, dict_file);
                args[running_threads - 1].outputQueue = outputQueue;
                pthread_create(&tid[running_threads - 1], NULL, threadFunction, &args[running_threads - 1]);
            }
        } else if (mainMenuOption == 2) {
            // user wants to exit program
            // check if any tasks still running
            if (running_threads == 0) {
                printf("Exiting program...\n");
                break;
            } else {
                // cleanup running threads
                int i;
                for (i = 0; i < running_threads; i++) {
                    pthread_cancel(tid[i]);
                }
                printf("Exiting program...\n");
                break;
            }
        }

        // display and queued output
        SpellcheckOutput output;
        if (deleteNode(outputQueue, &output)) {
            printf("Output for %s (input file) and %s (dictionary):\n", output.input_file, output.dict_file);
            printf("\tMistakes: %d\n", output.mistakes);
            printf("\tMost common incorrect words:\n");
            int i;
            for (i = 0; i < 5; i++) {
                printf("\t\t[%d] %s (occurrences = %d)\n", i + 1, output.incorrectWords[i].word, output.incorrectWords[i].count);
            }
            printf("\nEnter any input to continue.\n");
            //getchar();
            char ch;
            scanf(" %c", &ch);
            printf("\n\n");
        }
    }

    // free resources
    // wait for worker threads to terminate
    int i;
    for (i = 0; i < running_threads; i++) {
        pthread_join(tid[i], NULL);
    }
    free(tid);
    free(args);
    free(outputQueue);

    return 0;
}


// function performs spell checking of a given file in a new thread separate from main
void* threadFunction(void* arg) {
    threadArgs* args = (threadArgs*)arg;
    dictionary *dict = malloc(sizeof(dictionary));
    InputWord *inputWords = malloc(0);
    int bucketSizes[26]; // size of bucket
    int bucketIndexes[26]; // current index of bucket
    char word[70]; //longest word in english is 46 letters so all words should fit in 70 bytes
    int i, j;

    // register function to cleanup when thread terminates
    CleanupArgs cleanupArgs;
    cleanupArgs.dict = dict;
    cleanupArgs.inputWords = inputWords;
    pthread_cleanup_push(cleanupHandler, &cleanupArgs);
    
    for (i = 0; i < 26; i++) {
        bucketSizes[i] = 0;
        bucketIndexes[i] = 0;
    }

    // read from dictionary file (files were checked for errors in subMenu function)
    FILE *dict_fp = fopen(args->dict_file, "r");
    if (dict_fp == NULL) {
        printf("Error - could not open dictionary file. Cancelling task and returning to main menu...\n");
        fclose(dict_fp);
        return 0;
    }
    // first time reading through, only look at first letter of each word to count bucket sizes (number of words starting with each letter in alphabet) for dictionary
    do {
        fscanf(dict_fp, "%s", word);
        formatString(word);
        getBucketSizes(bucketSizes, word);
    } while (!feof(dict_fp));
    for (i = 0; i < 26; i++) {
        dict->buckets[i].size = bucketSizes[i];
        dict->buckets[i].words = malloc(bucketSizes[i] * sizeof(char*));
        for (j = 0; j < dict->buckets[i].size; j++) {
            dict->buckets[i].words[j] = malloc(70);
        }
    }

    // second time reading, read word and then sort it into proper bucket according to first letter of the word (case insensitive)
    rewind(dict_fp);
    do {
        fscanf(dict_fp, "%s", word);
        formatString(word);
        sortWord(dict, word, bucketIndexes);
    } while (!feof(dict_fp));
    fclose(dict_fp);

    // start reading input file to be spell checked
    FILE *input_fp = fopen(args->input_file, "r");
    if (input_fp == NULL) {
        printf("Error - could not open input text file. Cancelling task and returning to main menu...\n");
        fclose(input_fp);
        return 0;
    }
    // read to store input words and their occurrances
    int inputIndex = 0;
    rewind(input_fp);
    while (!feof(input_fp)) {
        bool found = false;
        fscanf(input_fp, "%s", word);
        formatString(word);
        for (i = 0; i < inputIndex; i++) {
            if (strcmp(word, inputWords[i].word) == 0) {
                // word already in array
                inputWords[i].count += 1;
                found = true;
                break;
            }
        }
        // new word to be added to array
        if (!found) {
            inputWords = (InputWord*)realloc(inputWords, sizeof(InputWord) * (inputIndex + 1));
            strcpy(inputWords[inputIndex].word, word);
            inputWords[inputIndex].count = 1;
            inputIndex++;
        }
    }
    fclose(input_fp);

    // check total number of mistakes and 5 most common incorrect words
    SpellcheckOutput output;
    for (i = 0; i < 5; i++) {
        strcpy(output.incorrectWords[i].word, "");
        output.incorrectWords[i].count = 0;
    }
    output.mistakes = 0;

    for (i = 0; i < inputIndex; i++) {
        // call function to check first letter then check all words in bucket based on letter 
        int found = -1;
        int bucketIndex = getBucket(inputWords[i].word);
        if (bucketIndex == -1) {
            // word starts with invalid character
            continue;
        }
        for (j = 0; j < dict->buckets[bucketIndex].size; j++) {
            if (strcmp(inputWords[i].word, dict->buckets[bucketIndex].words[j]) == 0) {
                found = j;
                break;
            }
        }
        if (found == -1) {
            // word is not in the dictionary provided (is an incorrect word)
            output.mistakes += 1;
            // check if incorrect word is already in the top mistakes array
            int k;
            bool alreadyTop5 = false;
            for (k = 0; k < 5; k++) {
                if (strcmp(output.incorrectWords[k].word, inputWords[i].word) == 0) {
                    alreadyTop5 = true;
                    break;
                }
            }
            if (!alreadyTop5) {
                orderTopMistakes(&output, inputWords[i]);
            }
        }
    }

    // free resources used in spellchecking
    for (i = 0; i < 26; i++) {
        for (j = 0; j < dict->buckets[i].size; j++) {
            free(dict->buckets[i].words[j]);
        }
        free(dict->buckets[i].words);
    }
    free(dict);
    free(inputWords);

    // insert node containing output into queue
    insertNode(args->outputQueue, output.mistakes, output.incorrectWords, args->input_file, args->dict_file);

    pthread_cleanup_pop(NULL);
    return NULL;
}


void *threadInput(void *arg) {
    threadInputArgs *args = (threadInputArgs*)arg;
    printf("1. Start a new spellchecking task\n2. Exit\n");
    printf("Enter 1 or 2: ");

    char tmp[100] = "";
    fgets(tmp, 100, stdin);
    if (strcmp(tmp, "1\n") == 0) {
        *(args->option) = 1;
    } else if (strcmp(tmp, "2\n") == 0) {
        *(args->option) = 2;
    }
    return NULL;
}


// initialize mutex and condition variable for node linked list
OutputQueue *createOutputQueue() {
    OutputQueue *outputQueue = (OutputQueue*)malloc(sizeof(OutputQueue));
    outputQueue->head = outputQueue->tail = NULL;
    pthread_mutex_init(&outputQueue->mutex, NULL);
    pthread_cond_init(&outputQueue->cond, NULL);
    return outputQueue;
}


// function inserts a node into the outputQueue linked list
void insertNode(OutputQueue *outputQueue, int mistakes, InputWord incorrectWords[5], char input_file[70], char dict_file[70]) {
    OutputNode *newNode = (OutputNode*)malloc(sizeof(OutputNode));
    strcpy(newNode->output.input_file, input_file);
    strcpy(newNode->output.dict_file, dict_file);
    newNode->output.mistakes = mistakes;
    int i;
    for (i = 0; i < 5; i++) {
        strcpy(newNode->output.incorrectWords[i].word, incorrectWords[i].word);
        newNode->output.incorrectWords[i].count = incorrectWords[i].count;
    }
    newNode->next = NULL;

    // critical section
    pthread_mutex_lock(&outputQueue->mutex);
    if (outputQueue->tail != NULL) {
        outputQueue->tail->next = newNode;
        outputQueue->tail = newNode;
    } else {
        outputQueue->tail = outputQueue->head = newNode;
    }
    pthread_cond_signal(&outputQueue->cond);
    pthread_mutex_unlock(&outputQueue->mutex);
}


// function removes a node from the outputQueue linked list
// nodes should be removed after spellchecking complete and output has been displayed
// returns 1 on success, otherwise 0
int deleteNode(OutputQueue *outputQueue, SpellcheckOutput *output) {
    int success = 0;
    pthread_mutex_lock(&outputQueue->mutex);
    // Wait for a signal telling us that there's something on the queue
    // If we get woken up but the queue is still empty, we go back to sleep
    while (outputQueue->head == NULL) {
        pthread_cond_wait(&outputQueue->cond, &outputQueue->mutex);
    }
    OutputNode *oldHead = outputQueue->head;
    *output = oldHead->output;
    outputQueue->head = oldHead->next;
    if (outputQueue->head == NULL) {
        outputQueue->tail = NULL;
    }
    free(oldHead);
    success = 1;
    pthread_mutex_unlock(&outputQueue->mutex);
    return success;
}


// function displays menu allowing user to initiate a spellchecking task or exit the program
// menu display operates in main thread, getting user input is threaded to allow interrupt when idle
int mainMenu() {
    pthread_t input_tid[1];
    threadInputArgs inputArg[1];
    // pthread_t *input_tid = malloc(sizeof(pthread_t));
    // threadInputArgs *inputArg = malloc(sizeof(threadInputArgs));
    int option = 0;
    inputArg->option = &option;
    do {
        // printf("1. Start a new spellchecking task\n2. Exit\n");
        // printf("Enter 1 or 2: ");
        // accept user input in a separate thread to allow finished spellchecking tasks to interrupt and display output
        pthread_create(&input_tid[0], NULL, threadInput, &inputArg[0]);
        //sleep(5);
        pthread_join(input_tid[0], NULL);
        printf("\n");
    } while (*(inputArg->option) != 1 && *(inputArg->option) != 2);
    return *(inputArg->option);
}


// function displays menu allowing user to provide input file and dictionary file for spellchecking task
int subMenu(char *input, char *dict) {
    printf("Enter the input text file (type \"back\" to return to main menu): ");
    scanf("%s", input);
    if (strcmp(input, "back") == 0) {
        printf("Cancelling task and returning to main menu...\n");
        return 0;
    }
    printf("Enter the dictionary file (type \"back\" to return to main menu): ");
    scanf("%s", dict);
    if (strcmp(dict, "back") == 0) {
        printf("Cancelling task and returning to main menu...\n");
        return 0;
    }
    return 1;
}


// function strips punctuation from string (except apostrophes and dashes) and casts all letters to lowercase (spellchecking will be case insensitive)
void formatString(char *str) {
    char formattedStr[70]= "";
    int formattedIndex = 0;
    int i;
    for (i = 0; i < strlen(str); i++) {
        if (isalpha(str[i]) != 0) {
            // character is a letter
            if (isupper(str[i])) {
                formattedStr[formattedIndex] = tolower(str[i]);
            } else {
                formattedStr[formattedIndex] = str[i];
            }
            formattedIndex++;
        } else if (str[i] == '\'' || str[i] == '-') {
            formattedStr[formattedIndex] = str[i];
            formattedIndex++;
        }
    }
    strcpy(str, formattedStr);
}


// function measures the size of each bucket
// each bucket represents all words starting with a certain letter
void getBucketSizes(int *counts, char word[70]) {
    switch (word[0]) {
        case 'a':
            counts[0] += 1;
            break;
        case 'b':
            counts[1] += 1;
            break;
        case 'c':
            counts[2] += 1;
            break;
        case 'd':
            counts[3] += 1;
            break;
        case 'e':
            counts[4] += 1;
            break;
        case 'f':
            counts[5] += 1;
            break;
        case 'g':
            counts[6] += 1;
            break;
        case 'h':
            counts[7] += 1;
            break;
        case 'i':
            counts[8] += 1;
            break;
        case 'j':
            counts[9] += 1;
            break;
        case 'k':
            counts[10] += 1;
            break;
        case 'l':
            counts[11] += 1;
            break;
        case 'm':
            counts[12] += 1;
            break;
        case 'n':
            counts[13] += 1;
            break;
        case 'o':
            counts[14] += 1;
            break;
        case 'p':
            counts[15] += 1;
            break;
        case 'q':
            counts[16] += 1;
            break;
        case 'r':
            counts[17] += 1;
            break;
        case 's':
            counts[18] += 1;
            break;
        case 't':
            counts[19] += 1;
            break;
        case 'u':
            counts[20] += 1;
            break;
        case 'v':
            counts[21] += 1;
            break;
        case 'w':
            counts[22] += 1;
            break;
        case 'x':
            counts[23] += 1;
            break;
        case 'y':
            counts[24] += 1;
            break;
        case 'z':
            counts[25] += 1;
            break;
    }
}


// function sorts given word into given dictionary
void sortWord(dictionary *dict, char word[70], int *indexes) {
    // sort according to first character of word
    int index;
    switch (word[0]) {
        case 'a':
            index = indexes[0];
            strcpy(dict->buckets[0].words[index], word);
            indexes[0] += 1;
            break;
        case 'b':
            index = indexes[1];
            strcpy(dict->buckets[1].words[index], word);
            indexes[1] += 1;
            break;
        case 'c':
            index = indexes[2];
            strcpy(dict->buckets[2].words[index], word);
            indexes[2] += 1;
            break;
        case 'd':
            index = indexes[3];
            strcpy(dict->buckets[3].words[index], word);
            indexes[3] += 1;
            break;
        case 'e':
            index = indexes[4];
            strcpy(dict->buckets[4].words[index], word);
            indexes[4] += 1;
            break;
        case 'f':
            index = indexes[5];
            strcpy(dict->buckets[5].words[index], word);
            indexes[5] += 1;
            break;
        case 'g':
            index = indexes[6];
            strcpy(dict->buckets[6].words[index], word);
            indexes[6] += 1;
            break;
        case 'h':
            index = indexes[7];
            strcpy(dict->buckets[7].words[index], word);
            indexes[7] += 1;
            break;
        case 'i':
            index = indexes[8];
            strcpy(dict->buckets[8].words[index], word);
            indexes[8] += 1;
            break;
        case 'j':
            index = indexes[9];
            strcpy(dict->buckets[9].words[index], word);
            indexes[9] += 1;
            break;
        case 'k':
            index = indexes[10];
            strcpy(dict->buckets[10].words[index], word);
            indexes[10] += 1;
            break;
        case 'l':
            index = indexes[11];
            strcpy(dict->buckets[11].words[index], word);
            indexes[11] += 1;
            break;
        case 'm':
            index = indexes[12];
            strcpy(dict->buckets[12].words[index], word);
            indexes[12] += 1;
            break;
        case 'n':
            index = indexes[13];
            strcpy(dict->buckets[13].words[index], word);
            indexes[13] += 1;
            break;
        case 'o':
            index = indexes[14];
            strcpy(dict->buckets[14].words[index], word);
            indexes[14] += 1;
            break;
        case 'p':
            index = indexes[15];
            strcpy(dict->buckets[15].words[index], word);
            indexes[15] += 1;
            break;
        case 'q':
            index = indexes[16];
            strcpy(dict->buckets[16].words[index], word);
            indexes[16] += 1;
            break;
        case 'r':
            index = indexes[17];
            strcpy(dict->buckets[17].words[index], word);
            indexes[17] += 1;
            break;
        case 's':
            index = indexes[18];
            strcpy(dict->buckets[18].words[index], word);
            indexes[18] += 1;
            break;
        case 't':
            index = indexes[19];
            strcpy(dict->buckets[19].words[index], word);
            indexes[19] += 1;
            break;
        case 'u':
            index = indexes[20];
            strcpy(dict->buckets[20].words[index], word);
            indexes[20] += 1;
            break;
        case 'v':
            index = indexes[21];
            strcpy(dict->buckets[21].words[index], word);
            indexes[21] += 1;
            break;
        case 'w':
            index = indexes[22];
            strcpy(dict->buckets[22].words[index], word);
            indexes[22] += 1;
            break;
        case 'x':
            index = indexes[23];
            strcpy(dict->buckets[23].words[index], word);
            indexes[23] += 1;
            break;
        case 'y':
            index = indexes[24];
            strcpy(dict->buckets[24].words[index], word);
            indexes[24] += 1;
            break;
        case 'z':
            index = indexes[25];
            strcpy(dict->buckets[25].words[index], word);
            indexes[25] += 1;
            break;
    }
}


// function returns the index of the bucket to search in
int getBucket(char word[70]) {
    switch (word[0]) {
        case 'a':
            return 0;
        case 'b':
            return 1;
        case 'c':
            return 2;
        case 'd':
            return 3;
        case 'e':
            return 4;
        case 'f':
            return 5;
        case 'g':
            return 6;
        case 'h':
            return 7;
        case 'i':
            return 8;
        case 'j':
            return 9;
        case 'k':
            return 10;
        case 'l':
            return 11;
        case 'm':
            return 12;
        case 'n':
            return 13;
        case 'o':
            return 14;
        case 'p':
            return 15;
        case 'q':
            return 16;
        case 'r':
            return 17;
        case 's':
            return 18;
        case 't':
            return 19;
        case 'u':
            return 20;
        case 'v':
            return 21;
        case 'w':
            return 22;
        case 'x':
            return 23;
        case 'y':
            return 24;
        case 'z':
            return 25;
    }
    return -1;
}

// function keeps track of the most common incorrect words
void orderTopMistakes(SpellcheckOutput *output, InputWord inputWord) {
    if (inputWord.count > output->incorrectWords[4].count) { // check 5th
        strcpy(output->incorrectWords[4].word, inputWord.word);
        output->incorrectWords[4].count = inputWord.count;
        if (inputWord.count > output->incorrectWords[3].count) { //check 4th
            strcpy(output->incorrectWords[4].word, output->incorrectWords[3].word);
            output->incorrectWords[4].count = output->incorrectWords[3].count;
            strcpy(output->incorrectWords[3].word, inputWord.word);
            output->incorrectWords[3].count = inputWord.count;
            if (inputWord.count > output->incorrectWords[2].count) { // check 3rd
                strcpy(output->incorrectWords[3].word, output->incorrectWords[2].word);
                output->incorrectWords[3].count = output->incorrectWords[2].count;
                strcpy(output->incorrectWords[2].word, inputWord.word);
                output->incorrectWords[2].count = inputWord.count;
                if (inputWord.count > output->incorrectWords[1].count) { // check 2nd
                    strcpy(output->incorrectWords[2].word, output->incorrectWords[1].word);
                    output->incorrectWords[2].count = output->incorrectWords[1].count;
                    strcpy(output->incorrectWords[1].word, inputWord.word);
                    output->incorrectWords[1].count = inputWord.count;
                    if (inputWord.count > output->incorrectWords[2].count) { // check 1st
                        strcpy(output->incorrectWords[1].word, output->incorrectWords[0].word);
                        output->incorrectWords[1].count = output->incorrectWords[0].count;
                        strcpy(output->incorrectWords[0].word, inputWord.word);
                        output->incorrectWords[0].count = inputWord.count;
                    }
                }
            }
        }
    }
}


// function handles spellchecking thread cleanup on abrupt termination
void cleanupHandler(void *arg) {
    CleanupArgs *args = (CleanupArgs*)arg;
    if (args->dict != NULL) {
        free(args->dict);
    }
    if (args->inputWords != NULL) {
        free(args->inputWords);
    }
}