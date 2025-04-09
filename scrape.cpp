#include <libxml/HTMLparser.h>
#include <libxml/xpath.h>
#include <sstream>
#include <iostream>

[[nodiscard]] std::string extractWithFormatting(htmlNodePtr node) {
    std::ostringstream oss;
    for (htmlNodePtr curr = node->children; curr != nullptr; curr = curr->next) {
        if (curr->type == xmlElementType::XML_TEXT_NODE && curr->content) {
            std::string_view temp {reinterpret_cast<const char*>(curr->content)};
            for (char ch: temp) {
                if (std::isprint(ch))
                    oss << ch;
            }
        } else if (curr->type == xmlElementType::XML_ELEMENT_NODE) {
            std::string tagName {curr->name? reinterpret_cast<const char*>(curr->name): ""};
            if (tagName == "br") oss << '\n';
            else if (tagName == "span") 
                oss << extractWithFormatting(curr);
            else if (tagName == "a") {
                for (xmlAttrPtr att = curr->properties; att != nullptr; att = att->next) {
                    if (xmlStrEqual(att->name, BAD_CAST "href")) {
                        xmlChar* hrefDump = xmlNodeGetContent(att->children);
                        if (hrefDump != nullptr) {
                            std::string href {reinterpret_cast<const char*>(hrefDump)};
                            std::string linkText {reinterpret_cast<const char*>(xmlNodeGetContent(curr))};
                            oss << (!linkText.starts_with("hashtag#")? "![" + linkText + "](" + href + ")": "#" + linkText.substr(8));
                        }
                        break;
                    }
                }
            }
        }
    }

    return oss.str();
}

int main() {
    // Parse the html file with no warnings
    htmlDocPtr doc = htmlReadFile("activity-all.htm", nullptr, HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING);

    // Check if parsing was successful
    if (doc == nullptr) {
        std::cerr << "Failed to parse file\n";
        return 1;
    }

    // Get the root element
    htmlNodePtr root = xmlDocGetRootElement(doc); 
    if (root == nullptr) {
        std::cerr << "Empty document";
        return 1;
    }

    // Extract all text inside ".update-text-component span span"
    xmlXPathContextPtr context = xmlXPathNewContext(doc);
    xmlXPathObjectPtr xpathResult = xmlXPathEvalExpression(reinterpret_cast<const xmlChar*>(
                "//div[contains(@class, 'update-components-text')]/span[1]/span[1]"), context);
    if (context == nullptr || xpathResult == nullptr) {
        std::cerr << "XPath Eval failed.\n";
        xmlXPathFreeContext(context);
        return 1;
    }

    // Print out the extracted node contents
    xmlNodeSetPtr nodes = xpathResult->nodesetval;
    for (int i = 0; i < nodes->nodeNr; i++) {
        htmlNodePtr node = nodes->nodeTab[i];
        std::cout << extractWithFormatting(node) << '\n';
        std::cout << std::string(10, '-') << '\n';
    }

    // Cleanup
    xmlXPathFreeObject(xpathResult);
    xmlXPathFreeContext(context);
    xmlFreeDoc(doc);
    xmlCleanupParser();
    return 0;
}
