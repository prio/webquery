#include "duckdb.hpp"
#include "duckdb/function/table_function.hpp"
#include "lexbor/html/html.h"

namespace duckdb {

struct HTMLReadGlobalState : public GlobalTableFunctionState {
    lxb_html_document_t *document;
    
    HTMLReadGlobalState() : document(nullptr) {}
    
    ~HTMLReadGlobalState() {
        if (document) {
            lxb_html_document_destroy(document);
        }
    }
};

// Function data structures
struct HTMLReadFunctionData : public TableFunctionData {
    string html_content;
    
    HTMLReadFunctionData(string html) : html_content(std::move(html)) {}
};

unique_ptr<FunctionData> ReadHTMLBind(ClientContext &context, 
                                    TableFunctionBindInput &input,
                                    vector<LogicalType> &return_types, 
                                    vector<string> &names) {

	auto result = make_uniq<HTMLReadFunctionData>();

	if (input.inputs.empty()) {
		throw InvalidInputException("read_html requires at least one argument (file pattern)");
	}    
    result->html_content = input.inputs[0].ToString();
    
    // Define output schema - adjust based on what you want to extract
    return_types.push_back(LogicalType::VARCHAR);  // The HTML element as string
    names.emplace_back("element");
    
    return std::move(result);
}

unique_ptr<GlobalTableFunctionState> ReadHTMLInit(ClientContext &context,
                                                          TableFunctionInitInput &input) {
    // TODO this is where files should be read from network, filesyatem etc.
    auto bind_data = input.bind_data->Cast<HTMLReadFunctionData>();
    auto state = make_uniq<HTMLReadGlobalState>();
    
    // Create lexbor document
    state->document = lxb_html_document_create();
    if (!state->document) {
        throw InternalException("Failed to create HTML document");
    }
    
    // Parse the HTML content
    lxb_status_t status = lxb_html_document_parse(state->document, 
                                                  (const lxb_char_t*)bind_data->html_content.c_str(),
                                                  bind_data->html_content.length());
    
    if (status != LXB_STATUS_OK) {
        throw InvalidInputException("Failed to parse HTML content");
    }
    
    return std::move(state);
}

void ReadHTMLFunction(ClientContext &context, TableFunctionInput &data_p, DataChunk &output) {
    auto &state = data_p.global_state->Cast<ReadHTMLGlobalState>();
    auto &bind_data = data_p.bind_data->Cast<HTMLReadFunctionData>();
    
    if (!state.document) {
        return; // No more data
    }
    
    // Example: Extract all elements and their text content
    lxb_dom_node_t *root = lxb_dom_interface_node(lxb_html_document_body_element(state.document));
    
    // Traverse the DOM and populate the output chunk
    // This is a simplified example - you'll want to implement proper traversal
    traverse_dom_tree(root, output);
    
    // Mark as finished after first chunk (adjust based on your needs)
    state.document = nullptr;
}

void traverse_dom_tree(lxb_dom_node_t *node, DataChunk &output) {
    // Implement DOM traversal logic here
    // Extract tag names, text content, attributes, etc.
    // Add rows to the output DataChunk
}

static void LoadInternal(DatabaseInstance &instance) {
    // Register READ_HTML table function
    TableFunction read_html_function(
        "read_html",
        {LogicalType::VARCHAR}, // html content
        ReadHTMLFunction,
        ReadHTMLBind,
        ReadHTMLInit
    ));    
	// read_html_function.named_parameters["ignore_errors"] = LogicalType::BOOLEAN;
	// read_html_function.named_parameters["maximum_file_size"] = LogicalType::BIGINT;    
    ExtensionUtil::RegisterFunction(instance, read_html_set);
    
    // // Register HTML_TEXT scalar function
    // ScalarFunctionSet html_text_set("html_text");
    // html_text_set.AddFunction(ScalarFunction(
    //     {LogicalType::VARCHAR, LogicalType::VARCHAR}, // html, selector
    //     LogicalType::VARCHAR, // return type
    //     html_text_function
    // ));
    
    // // Register indexed version for multi-element access
    // html_text_set.AddFunction(ScalarFunction(
    //     {LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::BIGINT}, // html, selector, index
    //     LogicalType::VARCHAR, // return type
    //     html_text_indexed_function
    // ));
    
    // ExtensionUtil::RegisterFunction(instance, html_text_set);
    
    // // Register HTML_ATTRIBUTE scalar function
    // ScalarFunctionSet html_attribute_set("html_attribute");
    // html_attribute_set.AddFunction(ScalarFunction(
    //     {LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR}, // html, selector, attribute
    //     LogicalType::VARCHAR, // return type
    //     html_attribute_function
    // ));
    
    // // Register indexed version for multi-element access
    // html_attribute_set.AddFunction(ScalarFunction(
    //     {LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::BIGINT}, // html, selector, attribute, index
    //     LogicalType::VARCHAR, // return type
    //     html_attribute_indexed_function
    // ));
    
    // ExtensionUtil::RegisterFunction(instance, html_attribute_set);
    
    // // Register HTML_COUNT scalar function
    // ScalarFunctionSet html_count_set("html_count");
    // html_count_set.AddFunction(ScalarFunction(
    //     {LogicalType::VARCHAR, LogicalType::VARCHAR}, // html, selector
    //     LogicalType::BIGINT, // return type
    //     html_count_function
    // ));
    
    // ExtensionUtil::RegisterFunction(instance, html_count_set);
}

void WebQueryExtension::Load(DuckDB &db) {
    LoadInternal(*db.instance);
}

std::string WebQueryExtension::Name() {
    return "webquery";
}

} // namespace duckdb

extern "C" {

DUCKDB_EXTENSION_API void webquery_init(duckdb::DatabaseInstance &db) {
    duckdb::DuckDB db_wrapper(db);
    db_wrapper.LoadExtension<duckdb::WebQueryExtension>();
}

DUCKDB_EXTENSION_API const char *webquery_version() {
    return duckdb::DuckDB::LibraryVersion();
}

}

// Extension entry point
#ifndef DUCKDB_EXTENSION_MAIN
#define DUCKDB_EXTENSION_MAIN

#include "duckdb.hpp"

namespace duckdb {

class WebQueryExtension : public Extension {
public:
    void Load(DuckDB &db) override;
    std::string Name() override;
};

} // namespace duckdb

#endif