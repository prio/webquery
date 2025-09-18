#define DUCKDB_EXTENSION_MAIN

#include "webquery_extension.hpp"

#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension_util.hpp"
#include "duckdb/function/table_function.hpp"

#include <duckdb/parser/parsed_data/create_scalar_function_info.hpp>
#include "duckdb/common/vector_operations/vector_operations.hpp"
#include <duckdb/common/types/value.hpp>

// Lexbor
#include <lexbor/html/html.h>
#include <lexbor/dom/dom.h>

using namespace duckdb;
using std::string;

// ---------- read_html Table Function ----------

struct HTMLBindData : public FunctionData {
    explicit HTMLBindData(string html_p, string selector_p) 
        : html(std::move(html_p)), selector(std::move(selector_p)) {}
    string html;
    string selector;
    
    unique_ptr<FunctionData> Copy() const override {
        return make_uniq<HTMLBindData>(html, selector);
    }
    
    bool Equals(const FunctionData &other_p) const override {
        auto &o = (const HTMLBindData &)other_p;
        return html == o.html && selector == o.selector;
    }
};

struct HTMLGlobalState : public GlobalTableFunctionState {
    vector<std::pair<lxb_dom_element_t*, string>> elements; // Store element pointer and serialized HTML
    idx_t current_idx = 0;
    lxb_html_document_t *doc = nullptr;
    
    ~HTMLGlobalState() {
        if (doc) {
            lxb_html_document_destroy(doc);
        }
    }
};

unique_ptr<FunctionData> html_bind(ClientContext &context, TableFunctionBindInput &input,
                                   vector<LogicalType> &return_types, vector<string> &names) {
    if (input.inputs.size() != 2) {
        throw InvalidInputException("read_html: expected 2 arguments (html string, css selector)");
    }
    string html = input.inputs[0].GetValue<string>();
    string selector = input.inputs[1].GetValue<string>();

    return_types.push_back(LogicalType::VARCHAR);  // The HTML element as string
    names.emplace_back("element");

    return make_uniq<HTMLBindData>(std::move(html), std::move(selector));
}

unique_ptr<GlobalTableFunctionState> html_init_global(ClientContext &context, TableFunctionInitInput &input) {
    auto result = make_uniq<HTMLGlobalState>();
    auto &bind_data = input.bind_data->Cast<HTMLBindData>();
    
    // Parse HTML
    result->doc = lxb_html_document_create();
    if (!result->doc) {
        throw std::bad_alloc();
    }
    
    lxb_status_t st = lxb_html_document_parse(result->doc,
        reinterpret_cast<const lxb_char_t *>(bind_data.html.data()),
        bind_data.html.size());
    if (st != LXB_STATUS_OK) {
        throw InvalidInputException("read_html: failed to parse HTML");
    }
    
    // Find elements matching the selector (simple tag name matching for now)
    lxb_dom_node_t *root = lxb_dom_interface_node(&result->doc->dom_document);
    
    std::function<void(lxb_dom_node_t*)> traverse = [&](lxb_dom_node_t *node) {
        if (!node) return;
        
        if (node->type == LXB_DOM_NODE_TYPE_ELEMENT) {
            lxb_dom_element_t *el = lxb_dom_interface_element(node);
            const lxb_char_t *tag_name = lxb_dom_element_qualified_name(el, NULL);
            
            if (tag_name && strcmp(reinterpret_cast<const char*>(tag_name), bind_data.selector.c_str()) == 0) {
                // Serialize element with attributes for display
                string element_str = "<";
                element_str += reinterpret_cast<const char*>(tag_name);
                
                // Get all attributes
                lxb_dom_attr_t *attr = lxb_dom_element_first_attribute(el);
                while (attr) {
                    const lxb_char_t *attr_name = lxb_dom_attr_qualified_name(attr, NULL);
                    const lxb_char_t *attr_value = lxb_dom_attr_value(attr, NULL);
                    
                    if (attr_name && attr_value) {
                        element_str += " ";
                        element_str += reinterpret_cast<const char*>(attr_name);
                        element_str += "=\"";
                        element_str += reinterpret_cast<const char*>(attr_value);
                        element_str += "\"";
                    }
                    
                    attr = lxb_dom_element_next_attribute(attr);
                }
                
                element_str += ">";
                
                // Get element content
                size_t text_len;
                const lxb_char_t *text = lxb_dom_node_text_content(lxb_dom_interface_node(el), &text_len);
                if (text) {
                    element_str += reinterpret_cast<const char*>(text);
                }
                
                element_str += "</";
                element_str += reinterpret_cast<const char*>(tag_name);
                element_str += ">";
                
                result->elements.push_back(std::make_pair(el, element_str));
            }
        }
        
        // Traverse children
        lxb_dom_node_t *child = lxb_dom_node_first_child(node);
        while (child) {
            traverse(child);
            child = lxb_dom_node_next(child);
        }
    };
    
    traverse(root);
    
    return std::move(result);
}

void html_main(ClientContext &context, TableFunctionInput &data, DataChunk &output) {
    auto &gstate = data.global_state->Cast<HTMLGlobalState>();
    
    if (gstate.current_idx >= gstate.elements.size()) {
        output.SetCardinality(0);
        return;
    }
    
    idx_t count = 0;
    idx_t remaining = gstate.elements.size() - gstate.current_idx;
    idx_t this_batch = std::min((idx_t)STANDARD_VECTOR_SIZE, remaining);
    
    for (idx_t i = 0; i < this_batch; i++) {
        auto &element_pair = gstate.elements[gstate.current_idx + i];
        output.SetValue(0, i, Value(element_pair.second)); // Use the serialized HTML string
        count++;
    }
    
    output.SetCardinality(count);
    gstate.current_idx += count;
}

// ---------- html_attribute Scalar Function ----------

struct HtmlAttrFun {
    static void Execute(DataChunk &args, ExpressionState &state, Vector &result) {
        auto count = args.size();
        
        UnaryExecutor::Execute<string_t, string_t>(
            args.data[0], result, count,
            [&](string_t element_str) -> string_t {
                auto element_html = element_str.GetString();
                
                // Extract selector and attribute from remaining args
                // This is a simplified version - in your case, we need access to the parsed elements
                
                // For now, return empty string - this needs the actual element context
                return StringVector::AddString(result, "");
            }
        );
    }
};

// Simple string-based attribute extraction
struct HtmlExtractAttrFun {
    static void Execute(DataChunk &args, ExpressionState &state, Vector &result) {
        auto count = args.size();
        
        BinaryExecutor::Execute<string_t, string_t, string_t>(
            args.data[0], args.data[1], result, count,
            [&](string_t element_str, string_t attr_name) -> string_t {
                auto element_html = element_str.GetString();
                auto attr = attr_name.GetString();
                
                // Simple string parsing approach
                string search_pattern = attr + "=\"";
                size_t start_pos = element_html.find(search_pattern);
                
                if (start_pos == string::npos) {
                    return StringVector::AddString(result, "");
                }
                
                start_pos += search_pattern.length();
                size_t end_pos = element_html.find("\"", start_pos);
                
                if (end_pos == string::npos) {
                    return StringVector::AddString(result, "");
                }
                
                string attr_value = element_html.substr(start_pos, end_pos - start_pos);
                return StringVector::AddString(result, attr_value);
            }
        );
    }
};

// ---------- Load Functions into DuckDB ----------

static void LoadInternal(DatabaseInstance &instance) {
    // read_html table function - now takes HTML and CSS selector
    TableFunction html_func(
        "read_html",
        {LogicalType::VARCHAR, LogicalType::VARCHAR},
        html_main,
        html_bind,
        html_init_global
    );
    ExtensionUtil::RegisterFunction(instance, html_func);

    // html_attribute scalar function - extracts attribute from HTML element string
    ScalarFunction html_attr_fun(
        "html_attribute",
        {LogicalType::VARCHAR, LogicalType::VARCHAR},
        LogicalType::VARCHAR,
        HtmlExtractAttrFun::Execute
    );
    ExtensionUtil::RegisterFunction(instance, html_attr_fun);
}

void WebqueryExtension::Load(DuckDB &db) {
    LoadInternal(*db.instance);
}

std::string WebqueryExtension::Name() {
    return "webquery";
}

std::string WebqueryExtension::Version() const {
#ifdef EXT_VERSION_WEBQUERY
    return EXT_VERSION_WEBQUERY;
#else
    return "";
#endif
}

// ---------- Entry Points ----------

extern "C" {

DUCKDB_EXTENSION_API void webquery_init(duckdb::DatabaseInstance &db) {
    duckdb::DuckDB db_wrapper(db);
    db_wrapper.LoadExtension<duckdb::WebqueryExtension>();
}

DUCKDB_EXTENSION_API const char *webquery_version() {
    return duckdb::DuckDB::LibraryVersion();
}

} // extern "C"

#ifndef DUCKDB_EXTENSION_MAIN
#error DUCKDB_EXTENSION_MAIN not defined
#endif