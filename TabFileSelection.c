#include "utility.h"
#include "TabFileSelection.h"
#include "LexicalAnalysis.h"
#include "syntaxanalysis.h"
#include "semantic_analysis.h"
#include "symboltable.h"
#include "ScriptExecutor.h"

// Function to map CommandType to string representation
const char* get_command_type_str(CommandType type) {
    switch (type) {
    case COMMAND_CONFIG_ELEM: return "CONFIG_ELEM";
    case COMMAND_DECLARE_VARIABLE: return "DECLARE_VARIABLE";
    case COMMAND_SHOW_PARAM: return "SHOW_PARAM";
    case COMMAND_GLOBAL_PICTURE: return "GLOBAL_PICTURE";
    case COMMAND_SUB_PICTURE: return "SUB_PICTURE";
    case COMMAND_USER_INPUT_PARAM: return "USER_INPUT_PARAM";
    case COMMAND_CHECKBOX_PARAM: return "CHECKBOX_PARAM";
    case COMMAND_USER_SELECT: return "USER_SELECT";
    case COMMAND_RADIOBUTTON_PARAM: return "RADIOBUTTON_PARAM";
    case COMMAND_BEGIN_TABLE: return "BEGIN_TABLE";
    case COMMAND_IF: return "IF";

        // Add other command types here as needed
    default: return "UNKNOWN";
    }
}

ProError ProcessTabFile(const wchar_t* tabFilePath) {
    ProGenericMsg(L"Starting ProcessTabFile");

    FILE* file;
    errno_t err = _wfopen_s(&file, tabFilePath, L"r");
    if (err != 0 || file == NULL) {
        ProGenericMsg(L"Failed to open the tab file");
        return PRO_TK_CANT_ACCESS;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        ProGenericMsg(L"Failed to seek to end of file");
        return PRO_TK_GENERAL_ERROR;
    }
    long size = ftell(file);
    if (size == -1) {
        fclose(file);
        ProGenericMsg(L"Failed to determine file size");
        return PRO_TK_GENERAL_ERROR;
    }
    rewind(file);

    char* buffer = (char*)malloc((size_t)size + 1);
    if (buffer == NULL) {
        fclose(file);
        ProGenericMsg(L"Memory allocation failed");
        return PRO_TK_CANT_ACCESS;
    }

    size_t read_size = fread(buffer, 1, (size_t)size, file);
    buffer[read_size] = '\0';
    if (read_size == 0) {
        ProGenericMsg(L"File is empty or read failed");
        free(buffer);
        fclose(file);
        return PRO_TK_GENERAL_ERROR;
    }

    Lexer lexer = {
        .cur_tok = buffer,
        .tokens = NULL,
        .token_count = 0,
        .capacity = 0,
        .line_number = 1,
        .line_start = buffer
    };

    int lex_result = lex(&lexer);
    if (lex_result != 0) {
        free(buffer);
        fclose(file);
        ProGenericMsg(L"Lexing error");
        return PRO_TK_GENERAL_ERROR;
    }

    ProPrintf(L"Generated %zu tokens", lexer.token_count);
    if (lexer.token_count == 0) {
        ProGenericMsg(L"No tokens generated");
    }

    // Optional: Log all tokens (unchanged)
    for (size_t i = 0; i < lexer.token_count; i++) {
        TokenData* token = &lexer.tokens[i];
        wchar_t wval[1024] = { 0 };
        if (token->val) {
            int wideSize = MultiByteToWideChar(CP_UTF8, 0, token->val, -1, NULL, 0);
            if (wideSize == 0 || wideSize > 1024) {
                ProPrintf(L"Token: %d, Value: (conversion failed), Line: %zu, Col: %zu",
                    token->type, token->loc.line, token->loc.col);
                continue;
            }
            MultiByteToWideChar(CP_UTF8, 0, token->val, -1, wval, wideSize);
        }
        LogOnlyPrintf(L"Token: %d, Value: %ls, Line: %zu, Col: %zu",
            token->type, wval, token->loc.line, token->loc.col);
    }

    SymbolTable* st = create_symbol_table();
    if (!st) {
        ProGenericMsg(L"Failed to create symbol table");
        free(buffer);
        fclose(file);
        return PRO_TK_GENERAL_ERROR;
    }

    // Parse blocks
    BlockList blocks = parse_blocks(&lexer, st);
    if (blocks.block_count == 0) {
        ProPrintf(L"No blocks parsed");
    }
    else {
        ProPrintf(L"Parsed %zu blocks", blocks.block_count);
        for (size_t i = 0; i < blocks.block_count; i++) {
            Block* block = &blocks.blocks[i];
            const char* type_str_narrow = block->type == BLOCK_ASM ? "ASM" :
                block->type == BLOCK_GUI ? "GUI" : "TAB";
            wchar_t type_str_wide[10];
            MultiByteToWideChar(CP_UTF8, 0, type_str_narrow, -1, type_str_wide, 10);
            ProPrintf(L"Block %zu: Type=%ls, Command Count=%zu",
                i, type_str_wide, block->command_count);

            for (size_t j = 0; j < block->command_count; j++) {
                CommandNode* cmd_node = block->commands[j];
                const char* cmd_type_str = get_command_type_str(cmd_node->type);
                wchar_t cmd_type_wide[20];
                MultiByteToWideChar(CP_UTF8, 0, cmd_type_str, -1, cmd_type_wide, 20);
                LogOnlyPrintf(L"  Command %zu: Type=%ls", j, cmd_type_wide);
            }
        }

        // Perform semantic analysis on the full AST
        perform_semantic_analysis(&blocks, st);
        




        // Execute the ASM block
        Block* asm_block = find_block(&blocks, BLOCK_ASM);
        if (asm_block) {
            for (size_t j = 0; j < asm_block->command_count; j++) {
                CommandNode* cmd = asm_block->commands[j];
                execute_command(cmd, st, &blocks);
            }
        }
        else {
            ProPrintf(L"No ASM block found");
        }


    }

    // Clean up
    free_symbol_table(st);
    free_block_list(&blocks);
    free_lexer(&lexer);
    free(buffer);
    fclose(file);
    return PRO_TK_NO_ERROR;
}

// Function to free the dynamically allocated array of file names
void FreeTabFiles(wchar_t** tabFiles, int fileCount)
{
    for (int i = 0; i < fileCount; i++)
    {
        free(tabFiles[i]);
    }
    free(tabFiles);
}

wchar_t** ListTabFiles(const wchar_t* directory, int* fileCount) {
    WIN32_FIND_DATAW findFileData;  // Use wide-character version
    HANDLE hFind = INVALID_HANDLE_VALUE;
    wchar_t searchPattern[MAX_PATH];
    wchar_t** tabFiles = NULL;
    *fileCount = 0;

    // Create a search pattern for .tab files
    swprintf(searchPattern, MAX_PATH, L"%s\\*.tab", directory);

    // Use FindFirstFileW for wide-character strings
    hFind = FindFirstFileW(searchPattern, &findFileData);

    if (hFind == INVALID_HANDLE_VALUE) {
        wprintf(L"Could not find any .tab files in directory: %s\n", directory);
        return NULL;
    }

    do {
        // Calculate the new size for tabFiles array
        size_t newSize = (size_t)(*fileCount) + 1;

        // Allocate a temporary pointer for realloc
        wchar_t** temp = (wchar_t**)realloc(tabFiles, newSize * sizeof(wchar_t*));
        if (temp == NULL) {
            wprintf(L"Memory allocation failed.\n");

            // Free previously allocated memory
            FreeTabFiles(tabFiles, *fileCount);

            return NULL;
        }

        // Assign the reallocated memory back to tabFiles
        tabFiles = temp;

        // Allocate memory for the new file name
        tabFiles[*fileCount] = (wchar_t*)malloc(MAX_PATH * sizeof(wchar_t));
        if (tabFiles[*fileCount] == NULL) {
            wprintf(L"Memory allocation failed for file name.\n");

            // Free previously allocated memory
            FreeTabFiles(tabFiles, *fileCount);

            return NULL;
        }

        // Copy the file name (which is wide-character in findFileData.cFileName) to the allocated space
        wcscpy_s(tabFiles[*fileCount], MAX_PATH, findFileData.cFileName);

        (*fileCount)++;
    } while (FindNextFileW(hFind, &findFileData) != 0);  // Use FindNextFileW for wide-character strings

    FindClose(hFind);

    return tabFiles;
}


// Double-click callback for executing a selected .tab file
void MyListDoubleClickCallback(char* dialog, char* component, ProAppData app_data)
{
    (void)app_data;  // Mark unused
    ProError status;
    int n_selections;
    char** selections;

    // Get the selected name from the list
    status = ProUIListSelectednamesGet(dialog, component, &n_selections, &selections);
    if (status == PRO_TK_NO_ERROR && n_selections > 0) {
        // Exit and destroy the dialog
        ProUIDialogExit(dialog, PRO_TK_NO_ERROR);  // Ensure dialog exit
        ProUIDialogDestroy(dialog);  // Ensure dialog destruction
        // Construct the file path
        ProTKSwprintf(selectedTabFilePath, L"C:\\emjacScript\\%hs", selections[0]);
        ProGenericMsg(L"Executing selected .tab file:");
        ProGenericMsg(selectedTabFilePath);

        // Call ProcessTabFile with the selected file path
        status = ProcessTabFile(selectedTabFilePath);
        if (status != PRO_TK_NO_ERROR) {
            ProGenericMsg(L"Failed to process .tab file.");
        }
        else {
            ProGenericMsg(L".tab file processed successfully.");
        }


    }
    else {
        ProGenericMsg(L"No item selected or error occurred");
    }

    ProStringarrayFree(selections, n_selections);
}

// Callback for the Cancel button
void MyAppCancelCallback(char* dialog, char* component, ProAppData app_data)
{
    (void)component;   // Mark unused
    (void)app_data;    // Mark unused
    ProUIDialogExit(dialog, PRO_TK_NO_ERROR);  // Exit the dialog
    ProUIDialogDestroy(dialog);
}


wchar_t** ListGphFiles(const wchar_t* directory, int* fileCount) {
    WIN32_FIND_DATAW findFileData;  // Use wide-character version
    HANDLE hFind = INVALID_HANDLE_VALUE;
    wchar_t searchPattern[MAX_PATH];
    wchar_t** tabFiles = NULL;
    *fileCount = 0;

    // Create a search pattern for .tab files
    swprintf(searchPattern, MAX_PATH, L"%s\\*.gph", directory);

    // Use FindFirstFileW for wide-character strings
    hFind = FindFirstFileW(searchPattern, &findFileData);

    if (hFind == INVALID_HANDLE_VALUE) {
        wprintf(L"Could not find any .tab files in directory: %s\n", directory);
        return NULL;
    }

    do {
        // Calculate the new size for tabFiles array
        size_t newSize = (size_t)(*fileCount) + 1;

        // Allocate a temporary pointer for realloc
        wchar_t** temp = (wchar_t**)realloc(tabFiles, newSize * sizeof(wchar_t*));
        if (temp == NULL) {
            wprintf(L"Memory allocation failed.\n");

            // Free previously allocated memory
            FreeTabFiles(tabFiles, *fileCount);

            return NULL;
        }

        // Assign the reallocated memory back to tabFiles
        tabFiles = temp;

        // Allocate memory for the new file name
        tabFiles[*fileCount] = (wchar_t*)malloc(MAX_PATH * sizeof(wchar_t));
        if (tabFiles[*fileCount] == NULL) {
            wprintf(L"Memory allocation failed for file name.\n");

            // Free previously allocated memory
            FreeTabFiles(tabFiles, *fileCount);

            return NULL;
        }

        // Copy the file name (which is wide-character in findFileData.cFileName) to the allocated space
        wcscpy_s(tabFiles[*fileCount], MAX_PATH, findFileData.cFileName);

        (*fileCount)++;
    } while (FindNextFileW(hFind, &findFileData) != 0);  // Use FindNextFileW for wide-character strings

    FindClose(hFind);

    return tabFiles;
}


void UpdateListWithTabFiles(char* dialog, const wchar_t* folderPath) {
    ProError status;
    wchar_t** tabFiles = NULL;
    int fileCount = 0;

    // List the .tab files in the selected folder
    tabFiles = ListTabFiles(folderPath, &fileCount);

    // Clear existing list items
    status = ProUIListNamesSet(dialog, "ListLibraryFolderContent", 0, NULL);
    if (status != PRO_TK_NO_ERROR) {
        ProGenericMsg(L"Could not clear list items");
        if (tabFiles != NULL) FreeTabFiles(tabFiles, fileCount);
        return;
    }

    if (tabFiles == NULL || fileCount == 0) {
        ProGenericMsg(L"No .tab files found in the selected folder");
        return;
    }

    // Allocate memory for names
    char** names = (char**)malloc(fileCount * sizeof(char*));
    if (names == NULL) {
        FreeTabFiles(tabFiles, fileCount);
        ProGenericMsg(L"Memory allocation failed for names array");
        return;
    }

    // Allocate memory for image paths
    char** imagePaths = (char**)malloc(fileCount * sizeof(char*));
    if (imagePaths == NULL) {
        FreeTabFiles(tabFiles, fileCount);
        for (int i = 0; i < fileCount; i++) free(names[i]);
        free(names);
        ProGenericMsg(L"Memory allocation failed for image paths array");
        return;
    }

    for (int i = 0; i < fileCount; i++) {
        // Allocate memory for each name and convert from wchar_t to char
        names[i] = (char*)malloc(MAX_PATH * sizeof(char));
        if (names[i] == NULL) {
            for (int j = 0; j < i; j++) free(names[j]);
            free(names);
            FreeTabFiles(tabFiles, fileCount);
            ProGenericMsg(L"Memory allocation failed for individual name");
            return;
        }
        wcstombs_s(NULL, names[i], MAX_PATH, tabFiles[i], MAX_PATH);

        // Allocate memory for each image path and set it to the same image
        imagePaths[i] = (char*)malloc(MAX_PATH * sizeof(char));
        if (imagePaths[i] == NULL) {
            for (int j = 0; j < i; j++) {
                free(names[j]);
                free(imagePaths[j]);
            }
            free(names);
            free(imagePaths);
            FreeTabFiles(tabFiles, fileCount);
            ProGenericMsg(L"Memory allocation failed for individual image path");
            return;
        }
        strcpy_s(imagePaths[i], MAX_PATH, "C:\\OriginalDirectory\\images\\file.png"); // Set the path to the icon
    }

    // Set the list names
    status = ProUIListNamesSet(dialog, "ListLibraryFolderContent", fileCount, names);
    if (status != PRO_TK_NO_ERROR) {
        ProGenericMsg(L"Could not set list names");
        for (int i = 0; i < fileCount; i++) {
            free(names[i]);
            free(imagePaths[i]);
        }
        free(names);
        free(imagePaths);
        FreeTabFiles(tabFiles, fileCount);
        return;
    }

    // Set the list labels using tabFiles
    status = ProUIListLabelsSet(dialog, "ListLibraryFolderContent", fileCount, tabFiles);
    if (status != PRO_TK_NO_ERROR) {
        ProGenericMsg(L"Could not set list labels");
        for (int i = 0; i < fileCount; i++) {
            free(names[i]);
            free(imagePaths[i]);
        }
        free(names);
        free(imagePaths);
        FreeTabFiles(tabFiles, fileCount);
        return;
    }

    // Set the images for each list item
    status = ProUIListItemimageSet(dialog, "ListLibraryFolderContent", fileCount, imagePaths);
    if (status != PRO_TK_NO_ERROR) {
        ProGenericMsg(L"Could not set list item images");
    }

    // Free allocated memory
    for (int i = 0; i < fileCount; i++) {
        free(names[i]);
        free(imagePaths[i]);
    }
    free(names);
    free(imagePaths);
    FreeTabFiles(tabFiles, fileCount);
}


void UpdateListWithUDFiles(char* dialog, const wchar_t* folderPath) {
    ProError status;
    wchar_t** udfFiles = NULL;
    int fileCount = 0;

    // List the .gph files in the selected folder (C:\emjacScript\UDF)
    udfFiles = ListGphFiles(folderPath, &fileCount);

    // Clear existing list items
    status = ProUIListNamesSet(dialog, "ListLibraryFolderContent", 0, NULL);
    if (status != PRO_TK_NO_ERROR) {
        ProGenericMsg(L"Could not clear list items");
        if (udfFiles != NULL) FreeTabFiles(udfFiles, fileCount);
        return;
    }

    if (udfFiles == NULL || fileCount == 0) {
        ProGenericMsg(L"No .gph files found in the selected folder");
        return;
    }

    // Allocate memory for names
    char** names = (char**)malloc(fileCount * sizeof(char*));
    if (names == NULL) {
        FreeTabFiles(udfFiles, fileCount);
        ProGenericMsg(L"Memory allocation failed for names array");
        return;
    }

    // Allocate memory for image paths
    char** imagePaths = (char**)malloc(fileCount * sizeof(char*));
    if (imagePaths == NULL) {
        FreeTabFiles(udfFiles, fileCount);
        for (int i = 0; i < fileCount; i++) free(names[i]);
        free(names);
        ProGenericMsg(L"Memory allocation failed for image paths array");
        return;
    }

    for (int i = 0; i < fileCount; i++) {
        // Allocate memory for each name and convert from wchar_t to char
        names[i] = (char*)malloc(MAX_PATH * sizeof(char));
        if (names[i] == NULL) {
            for (int j = 0; j < i; j++) free(names[j]);
            free(names);
            FreeTabFiles(udfFiles, fileCount);
            ProGenericMsg(L"Memory allocation failed for individual name");
            return;
        }
        wcstombs_s(NULL, names[i], MAX_PATH, udfFiles[i], MAX_PATH);

        // Allocate memory for each image path and set it to the same image
        imagePaths[i] = (char*)malloc(MAX_PATH * sizeof(char));
        if (imagePaths[i] == NULL) {
            for (int j = 0; j < i; j++) {
                free(names[j]);
                free(imagePaths[j]);
            }
            free(names);
            free(imagePaths);
            FreeTabFiles(udfFiles, fileCount);
            ProGenericMsg(L"Memory allocation failed for individual image path");
            return;
        }
        strcpy_s(imagePaths[i], MAX_PATH, "C:\\OriginalDirectory\\images\\file.png"); // Set the path to the icon
    }

    // Set the list names
    status = ProUIListNamesSet(dialog, "ListLibraryFolderContent", fileCount, names);
    if (status != PRO_TK_NO_ERROR) {
        ProGenericMsg(L"Could not set list names");
        for (int i = 0; i < fileCount; i++) {
            free(names[i]);
            free(imagePaths[i]);
        }
        free(names);
        free(imagePaths);
        FreeTabFiles(udfFiles, fileCount);
        return;
    }

    // Set the list labels using udfFiles
    status = ProUIListLabelsSet(dialog, "ListLibraryFolderContent", fileCount, udfFiles);
    if (status != PRO_TK_NO_ERROR) {
        ProGenericMsg(L"Could not set list labels");
        for (int i = 0; i < fileCount; i++) {
            free(names[i]);
            free(imagePaths[i]);
        }
        free(names);
        free(imagePaths);
        FreeTabFiles(udfFiles, fileCount);
        return;
    }

    // Set the images for each list item
    status = ProUIListItemimageSet(dialog, "ListLibraryFolderContent", fileCount, imagePaths);
    if (status != PRO_TK_NO_ERROR) {
        ProGenericMsg(L"Could not set list item images");
    }

    // Free allocated memory
    for (int i = 0; i < fileCount; i++) {
        free(names[i]);
        free(imagePaths[i]);
    }
    free(names);
    free(imagePaths);
    FreeTabFiles(udfFiles, fileCount);
}



// Callback function to toggle list visibility and update content
ProError EmjacScriptSelectCallback(char* dialog, char* component, ProAppData app_data) {
    (void)app_data;
    ProError status;
    char** selected_node_name = NULL;
    int num_selected = 0;

    // Retrieve the name of the selected node(s)
    status = ProUITreeSelectednamesGet(dialog, component, &num_selected, &selected_node_name);
    if (status != PRO_TK_NO_ERROR) {
        ProGenericMsg(L"Could not retrieve selected node name");
        return status;
    }

    // If no nodes are selected, clear or hide the list
    if (num_selected == 0) {
        status = ProUIListNamesSet(dialog, "ListLibraryFolderContent", 0, NULL);
        if (status != PRO_TK_NO_ERROR) {
            ProGenericMsg(L"Could not clear list items on deselection");
            return status;
        }
        // Hide the list component
        status = ProUIListHide(dialog, "ListLibraryFolderContent");
        if (status != PRO_TK_NO_ERROR) {
            ProGenericMsg(L"Could not hide list");
            return status;
        }
    }
    // If "emjacScript" is selected, display its contents
    else if (strcmp(selected_node_name[0], "emjacScript") == 0) {
        UpdateListWithTabFiles(dialog, L"C:\\emjacScript");
        status = ProUIListShow(dialog, "ListLibraryFolderContent");
        if (status != PRO_TK_NO_ERROR) {
            ProGenericMsg(L"Could not show list for emjacScript");
        }
    }
    // If "udf" is selected, display its contents
    else if (strcmp(selected_node_name[0], "udf") == 0) {
        UpdateListWithUDFiles(dialog, L"C:\\emjacScript\\UDF");
        status = ProUIListShow(dialog, "ListLibraryFolderContent");
        if (status != PRO_TK_NO_ERROR) {
            ProGenericMsg(L"Could not show list for udf");
        }
    }
    // If any other node is selected, clear or hide the list
    else {
        status = ProUIListNamesSet(dialog, "ListLibraryFolderContent", 0, NULL);
        if (status != PRO_TK_NO_ERROR) {
            ProGenericMsg(L"Could not clear list items for other nodes");
            ProArrayFree((ProArray*)&selected_node_name);
            return status;
        }
        // Hide the list component
        status = ProUIListHide(dialog, "ListLibraryFolderContent");
        if (status != PRO_TK_NO_ERROR) {
            ProGenericMsg(L"Could not hide list for other nodes");
            ProArrayFree((ProArray*)&selected_node_name);
            return status;
        }
    }

    // Free the selected node name array
    ProArrayFree((ProArray*)&selected_node_name);
    return PRO_TK_NO_ERROR;
}


// Function to set selection callback on the emjacScript node
ProError SetEmjacScriptSelectCallback(char* dialog, char* treeview_component) {
    ProError status;

    // Set the selection callback on the emjacScript node to toggle the list visibility
    status = ProUITreeSelectActionSet(dialog, treeview_component, EmjacScriptSelectCallback, NULL);
    if (status != PRO_TK_NO_ERROR) {
        ProGenericMsg(L"Failed to set selection callback for emjacScript node");
        return status;
    }
    return PRO_TK_NO_ERROR;
}



ProError AddTreeNode(char* dialog, char* treeview_component) {
    ProError status;
    ProUITreeNodeType folderType;

    // Allocate node type "dir"
    status = ProUITreeNodeTypeAlloc("dir", &folderType);
    if (status != PRO_TK_NO_ERROR) {
        ProGenericMsg(L"ProUITreeNodeTypeAlloc failed for dir");
        return status;
    }

    // Set images for collapsed and expanded states
    status = ProUITreeNodeTypeCollapseImageSet(folderType, "C:\\OriginalDirectory\\images\\Closed.png");
    if (status != PRO_TK_NO_ERROR) {
        ProGenericMsg(L"Failed to set collapsed image");
        ProUITreeNodeTypeFree(&folderType);
        return status;
    }

    status = ProUITreeNodeTypeExpandImageSet(folderType, "C:\\OriginalDirectory\\images\\Opened.png");
    if (status != PRO_TK_NO_ERROR) {
        ProGenericMsg(L"Failed to set expanded image");
        ProUITreeNodeTypeFree(&folderType);
        return status;
    }

    // Add root node "directory"
    status = ProUITreeNodeAdd(dialog, treeview_component, "directory", L"Directory", NULL, folderType);
    if (status != PRO_TK_NO_ERROR) {
        ProGenericMsg(L"ProUITreeNodeAdd failed for root node directory");
        ProUITreeNodeTypeFree(&folderType);
        return status;
    }

    // Add "emjacScript" node under "directory"
    status = ProUITreeNodeAdd(dialog, treeview_component, "emjacScript", L"emjacScript", "directory", folderType);
    if (status != PRO_TK_NO_ERROR) {
        ProGenericMsg(L"ProUITreeNodeAdd failed for node emjacScript");
        ProUITreeNodeTypeFree(&folderType);
        return status;
    }

    // Optionally, add a placeholder child to make it expandable
    status = ProUITreeNodeAdd(dialog, treeview_component, "udf", L"UDF", "emjacScript", folderType);
    if (status != PRO_TK_NO_ERROR) {
        ProGenericMsg(L"ProUITreeNodeAdd failed for placeholder under emjacScript");
        ProUITreeNodeTypeFree(&folderType);
        return status;
    }

    status = ProUITreeNodeAdd(dialog, treeview_component, "newdirectory", L"New Directory", "directory", folderType);
    if (status != PRO_TK_NO_ERROR)
    {
        ProGenericMsg(L"ProUITreeNodeAdd failed to add New Directory Node");
        return status;
    }

    // Expand the "directory" node to show its children
    status = ProUITreeNodeExpand(dialog, treeview_component, "directory", PRO_B_TRUE);

    // Redraw the tree to display all nodes
    ProUITreeTreeredrawSet(dialog, treeview_component, PRO_B_TRUE);

    return PRO_TK_NO_ERROR;
}

// The main function to set up the dialog
ProError esMenu()
{
    ProError status;
    int dialog_status;
    char* dialog = "ug_uilist";          // Dialog name
    char* list_component = "ListLibraryFolderContent";  // List component name
    char* tree_component = "TreeLibraryFolders";    // Tree Component Name

    status = ProUIDialogCreate(dialog, dialog);
    if (status != PRO_TK_NO_ERROR) {
        ProGenericMsg(L"Could not create dialog");
        return status;
    }

    status = ProUIListShow(dialog, list_component);
    if (status != PRO_TK_NO_ERROR) {
        ProGenericMsg(L"Could not show file list");
        ProUIDialogDestroy(dialog);
        return status;
    }

    status = ProUIListColumnsSet(dialog, list_component, 5);
    if (status != PRO_TK_NO_ERROR) {
        ProGenericMsg(L"Failed to set list columns");
        ProUIDialogDestroy(dialog);
        return status;
    }

    status = AddTreeNode(dialog, tree_component);
    if (status != PRO_TK_NO_ERROR) {
        ProGenericMsg(L"Failed to add tree nodes");
        ProUIDialogDestroy(dialog);
        return status;
    }

    SetEmjacScriptSelectCallback(dialog, tree_component);

    status = ProUIListSelectActionSet(dialog, list_component, MyListDoubleClickCallback, NULL);
    if (status != PRO_TK_NO_ERROR)
    {
        ProGenericMsg(L"Could not access List Selected Action");
        return status;
    }

    status = ProUIPushbuttonActivateActionSet(dialog, "ug_uilist_cancel", (ProUIAction)MyAppCancelCallback, NULL);
    if (status != PRO_TK_NO_ERROR) {
        ProGenericMsg(L"Could not activate Cancel button");
        ProUIDialogDestroy(dialog);
        return status;
    }

    // Activate the dialog
    status = ProUIDialogActivate(dialog, &dialog_status);
    if (status != PRO_TK_NO_ERROR) {
        ProGenericMsg(L"Could not activate dialog");
        ProUIDialogDestroy(dialog);
        return status;
    }


    status = ProUIDialogExit(dialog, PRO_TK_NO_ERROR);
    if (status != PRO_TK_NO_ERROR)
    {
        ProGenericMsg(L"Dialog Exit");
        return status;
    }

    // Clean up the dialog
    status = ProUIDialogDestroy(dialog);
    if (status != PRO_TK_NO_ERROR) {
        ProGenericMsg(L"Could not destroy dialog");
        return status;
    }

    return PRO_TK_NO_ERROR;
}