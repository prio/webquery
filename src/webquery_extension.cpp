#define DUCKDB_EXTENSION_MAIN

#include "webquery_extension.hpp"

#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension_util.hpp"
#include "duckdb/function/table_function.hpp"

#include "duckdb/catalog/default/default_functions.hpp"
#include <duckdb/parser/parsed_data/create_scalar_function_info.hpp>
#include "duckdb/common/vector_operations/vector_operations.hpp"
#include <duckdb/common/types/value.hpp>

// Lexbor
#include <lexbor/html/html.h>
#include <lexbor/dom/dom.h>
#include <lexbor/css/css.h>
#include <lexbor/selectors/selectors.h>


using namespace duckdb;
using std::string;

namespace duckdb {

static const DefaultMacro HTML_MACROS[] = {
    {DEFAULT_SCHEMA,
     "html_find_attr",
     {"e", "s", "a", nullptr},
     {{nullptr, nullptr}},
     "html_attribute(html_find(e, s), a);"},

    {nullptr, nullptr, {nullptr}, {{nullptr, nullptr}}, nullptr}};
}

struct HTMLBindData : public FunctionData {
    string html;
    string selector;

    explicit HTMLBindData(string html_p, string selector_p) 
        : html(std::move(html_p)), selector(std::move(selector_p)) {}
    
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

struct ElementInfo {
    string tag;                    // HTML tag name (div, span, p, etc.)
    lxb_dom_node_t *node;         // Pointer to the lexbor DOM node
    string html;  // Full HTML representation
    
    ElementInfo() : node(nullptr) {}
    
    ElementInfo(string tag_name, lxb_dom_node_t *dom_node, string html_str = "") 
        : tag(std::move(tag_name)), node(dom_node), html(std::move(html_str)) {}
};

struct SearchResult {
    vector<ElementInfo> elements;
    int count = 0;
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

lxb_status_t find_callback(lxb_dom_node_t *node, lxb_css_selector_specificity_t spec, void *ctx)
{
    // Cast context back to your data structure
    auto *data = static_cast<SearchResult*>(ctx);
    
    // Extract element info
    if (node->type == LXB_DOM_NODE_TYPE_ELEMENT) {
        lxb_dom_element_t *element = lxb_dom_interface_element(node);
        const lxb_char_t *tag_name = lxb_dom_element_qualified_name(element, nullptr);
        
        // Serialize the node and all its children
        lexbor_str_t str = {0};        
        lxb_status_t status = lxb_html_serialize_tree_str(node, &str);

        string full_node_html;
        if (status == LXB_STATUS_OK && str.data) {
            full_node_html = string((char*)str.data, str.length);
        } else {
            // throw InvalidInputException("read_html: failed to serialise node");
            return LXB_STATUS_ERROR;
        }

        // Store in your data structure
        data->elements.push_back(ElementInfo(string((char*)tag_name), node, full_node_html));        
        data->count++;

        // TODO
        // Clean up the string buffer
        // if (str.data) {
        //     lexbor_str_destroy(&str, nullptr, false);
        // }        
    }
    return LXB_STATUS_OK;
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

    /* Memory for all parsed structures. */
    auto memory = lxb_css_memory_create();
    auto status = lxb_css_memory_init(memory, 128);
    if (status != LXB_STATUS_OK) {
        throw InvalidInputException("read_html: failed to allocate memory");
    }

    /* Create CSS parser. */
    auto parser = lxb_css_parser_create();
    status = lxb_css_parser_init(parser, NULL);
    if (status != LXB_STATUS_OK) {
        throw InvalidInputException("read_html: failed to create CSS parser");
    }

    lxb_css_parser_memory_set(parser, memory);

    /* Create CSS Selector parser. */
    auto css_selectors = lxb_css_selectors_create();
    status = lxb_css_selectors_init(css_selectors);
    if (status != LXB_STATUS_OK) {
        throw InvalidInputException("read_html: failed to create CSS selector parser");
    }

    /* It is important that a new selector object is not created internally
     * for each call to the parser.
     */
    lxb_css_parser_selectors_set(parser, css_selectors);

    /* Selectors. */
    auto  selectors = lxb_selectors_create();
    status = lxb_selectors_init(selectors);
    if (status != LXB_STATUS_OK) {
        throw InvalidInputException("read_html: failed to init selectors");
    }    

    /* Parse and get the log. */
    const lxb_char_t* selector_ptr = (const lxb_char_t*)bind_data.selector.c_str();
    auto list_one = lxb_css_selectors_parse(parser, selector_ptr, bind_data.selector.length());
    if (list_one == NULL) {
        throw InvalidInputException("read_html: failed to apply CSS parser");
    }

    /* Find HTML nodes by CSS Selectors. */
    SearchResult sresult;
    status = lxb_selectors_find(selectors, root, list_one, find_callback, &sresult);
    if (status != LXB_STATUS_OK) {
        throw InvalidInputException("read_html: failed to fid with selectors");
    }    

    for (const auto& elem_info : sresult.elements) {
        // string element_str = elem_info.tag;  
        lxb_dom_element_t *el = lxb_dom_interface_element(elem_info.node);

        // std::cout << "===========================================================================" << std::endl;
        // std::cout << "html: " << elem_info.html << std::endl;
        // std::cout << "===========================================================================" << std::endl;

        // Create the pair and add to result
        result->elements.push_back(std::make_pair(el, elem_info.html));
    }    
    (void) lxb_css_selectors_destroy(css_selectors, true);
    
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


// void html_extract_attr_fun(DataChunk &args, ExpressionState &state, Vector &result) {
//     D_ASSERT(args.ColumnCount() == 2);    
//     auto &element_vector = args.data[0];
//     auto &attr_name_vector = args.data[1];    
//     BinaryExecutor::Execute<string_t, string_t, string_t>(
//         element_vector,
//         attr_name_vector,
//         result,
//         args.size(),
//         [&](string_t element_data, string_t attr_name_data) -> string_t {
//             string html_element = element_data.GetString();
//             string attribute_name = attr_name_data.GetString();
            
//             // std::cout << "===========================================================================" << std::endl;
//             // std::cout << "html_element: " << html_element << std::endl;            
//             // std::cout << "attribute_name: " << attribute_name << std::endl;
//             // std::cout << "===========================================================================" << std::endl;

//             // Parse HTML with lexbor
//             auto parser = lxb_html_parser_create();
//             auto status = lxb_html_parser_init(parser);

//             if (status != LXB_STATUS_OK) {
//                 //return EXIT_FAILURE;
//             }            
//             lxb_html_document_t *document = lxb_html_document_create();
//             document = lxb_html_parse(parser, 
//                                       (const lxb_char_t*)html_element.c_str(), 
//                                       html_element.length());         

//             string result_value = "";
            
//             // Get body and find first element
//             lxb_html_body_element_t *body = lxb_html_document_body_element(document);
//             lxb_dom_element_t *element = lxb_dom_interface_element(body);
            
//             std::cout << "===========================================================================" << std::endl;
//             std::cout << "tag_name: " << element->upper_name << std::endl;            
//             std::cout << "===========================================================================" << std::endl;                    

//             if (element) {
//                 size_t attr_len;
//                 const lxb_char_t *attr_value = lxb_dom_element_get_attribute(
//                     element,
//                     (const lxb_char_t*)attribute_name.c_str(),
//                     attribute_name.length(),
//                     &attr_len
//                 );
                
//                 if (attr_value) {
//                     result_value = string((char*)attr_value, attr_len);
//                 }
//             }            
            
//             // Cleanup
//             // lxb_html_document_destroy(document);   
//             // /* Destroy parser */
//             // lxb_html_parser_destroy(parser);                     
//             return StringVector::AddString(result, result_value);
//         }
//     );
// }
// Simple string-based attribute extraction
void html_extract_attr_fun(DataChunk &args, ExpressionState &state, Vector &result) {
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

// Find the first matching element
void html_find_fun(DataChunk &args, ExpressionState &state, Vector &resultVector) {
    D_ASSERT(args.ColumnCount() == 2);
    
    auto &html_vector = args.data[0];
    auto &selector_vector = args.data[1];
    
    BinaryExecutor::Execute<string_t, string_t, string_t>(
        html_vector,
        selector_vector,
        resultVector,
        args.size(),
        [&](string_t html_data, string_t selector_data) -> string_t {
            try {
                auto result = make_uniq<HTMLGlobalState>();                
                // Convert to std::string
                string html_content = html_data.GetString();
                string css_selector = selector_data.GetString();

                // Validate inputs
                if (html_content.empty() || css_selector.empty()) {
                    return StringVector::AddString(resultVector, "");
                }

                // Parse HTML
                result->doc = lxb_html_document_create();
                if (!result->doc) {
                    throw std::bad_alloc();
                }
                
                lxb_status_t st = lxb_html_document_parse(result->doc,
                    reinterpret_cast<const lxb_char_t *>(html_content.data()),
                    html_content.size());
                if (st != LXB_STATUS_OK) {
                    throw InvalidInputException("html_find: failed to parse HTML");
                }
                
                // Find elements matching the selector (simple tag name matching for now)
                lxb_dom_node_t *root = lxb_dom_interface_node(&result->doc->dom_document);

                /* Memory for all parsed structures. */
                auto memory = lxb_css_memory_create();
                auto status = lxb_css_memory_init(memory, 128);
                if (status != LXB_STATUS_OK) {
                    throw InvalidInputException("html_find: failed to allocate memory");
                }

                /* Create CSS parser. */
                auto parser = lxb_css_parser_create();
                status = lxb_css_parser_init(parser, NULL);
                if (status != LXB_STATUS_OK) {
                    throw InvalidInputException("html_find: failed to create CSS parser");
                }

                lxb_css_parser_memory_set(parser, memory);

                /* Create CSS Selector parser. */
                auto css_selectors = lxb_css_selectors_create();
                status = lxb_css_selectors_init(css_selectors);
                if (status != LXB_STATUS_OK) {
                    throw InvalidInputException("html_find: failed to create CSS selector parser");
                }

                /* It is important that a new selector object is not created internally
                * for each call to the parser.
                */
                lxb_css_parser_selectors_set(parser, css_selectors);

                /* Selectors. */
                auto selectors = lxb_selectors_create();
                status = lxb_selectors_init(selectors);
                if (status != LXB_STATUS_OK) {
                    throw InvalidInputException("html_find: failed to init selectors");
                }    

                /* Parse and get the log. */
                const lxb_char_t* selector_ptr = (const lxb_char_t*)css_selector.c_str();
                auto list_one = lxb_css_selectors_parse(parser, selector_ptr, css_selector.length());
                if (list_one == NULL) {
                    throw InvalidInputException("html_find: failed to apply CSS parser");
                }

                /* Find HTML nodes by CSS Selectors. */
                SearchResult sresult;
                status = lxb_selectors_find(selectors, root, list_one, find_callback, &sresult);
                if (status != LXB_STATUS_OK) {
                    throw InvalidInputException("html_find: failed to fid with selectors");
                }    

                for (const auto& elem_info : sresult.elements) {
                    // string element_str = elem_info.tag;  
                    lxb_dom_element_t *el = lxb_dom_interface_element(elem_info.node);

                    // Create the pair and add to result
                    result->elements.push_back(std::make_pair(el, elem_info.html));
                }    
                (void) lxb_css_selectors_destroy(css_selectors, true);                
                
                // Return result
                return StringVector::AddString(resultVector, sresult.elements[0].html);
                
            } catch (const std::exception &e) {
                // Return empty string on error
                return StringVector::AddString(resultVector, "");
            }
        }
    );
}


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

    ScalarFunctionSet html_find_set("html_find");    
    html_find_set.AddFunction(ScalarFunction(
        {LogicalType::VARCHAR, LogicalType::VARCHAR}, // html_string, css_selector
        LogicalType::VARCHAR,                         // returns string
        html_find_fun
    ));    
    ExtensionUtil::RegisterFunction(instance, html_find_set);    

    // html_text scalar function - extracts attribute from HTML element string
    // ScalarFunction html_text_fun(
    //     "html_text",
    //     {LogicalType::VARCHAR, LogicalType::VARCHAR},
    //     LogicalType::VARCHAR,
    //     HtmlExtractAttrFun::Execute
    // );
    // ExtensionUtil::RegisterFunction(instance, html_text_fun);
    
    // html_attribute scalar function - extracts attribute from HTML element string
    ScalarFunction html_attribute_fun(
        "html_attribute",
        {LogicalType::VARCHAR, LogicalType::VARCHAR}, // html_string, attribute_name
        LogicalType::VARCHAR,                         // returns string
        html_extract_attr_fun
    );
    ExtensionUtil::RegisterFunction(instance, html_attribute_fun);  
    // Alias
    ScalarFunction html_attr_fun(
        "html_attr",
        {LogicalType::VARCHAR, LogicalType::VARCHAR}, // html_string, attribute_name
        LogicalType::VARCHAR,                         // returns string
        html_extract_attr_fun
    );
    ExtensionUtil::RegisterFunction(instance, html_attr_fun);  

	// HTML macro's
	for (idx_t index = 0; HTML_MACROS[index].name != nullptr; index++) {
		auto info = DefaultFunctionGenerator::CreateInternalMacroInfo(HTML_MACROS[index]);
		// loader.RegisterFunction(*info);
        ExtensionUtil::RegisterFunction(instance, *info);
	}    
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