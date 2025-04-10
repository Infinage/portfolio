// g++ scrape.cpp -std=c++23 -o scraper -lxml2 -I/usr/include/libxml2 && ./scraper

// TODO
// 1. Resolve linkedin shortened urls
// 2. Write the hashtags to a seperate file

#include <libxml/HTMLparser.h>
#include <libxml/xpath.h>
#include <sstream>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

[[nodiscard]] std::string int2Str(int n, int pad = 0) {
    std::ostringstream tmpOss;
    tmpOss << std::setw(pad) << std::setfill('0') << n;
    return tmpOss.str();
}

[[nodiscard]] std::string urlDecode(const std::string &encoded) {
    std::istringstream iss {encoded};
    std::ostringstream oss;

    char hex; int val;
    while (iss >> std::noskipws >> hex) {
        if (hex != '%') oss << hex;
        else {
            if (iss >> std::hex >> val)
                oss << static_cast<char>(val);
        }
    }

    return oss.str();
}

[[nodiscard]] std::string extractTextWithFormatting(htmlNodePtr node) {
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
            if (tagName == "br") oss << "  \n";
            else if (tagName == "span") 
                oss << extractTextWithFormatting(curr);
            else if (tagName == "a") {
                for (xmlAttrPtr att = curr->properties; att != nullptr; att = att->next) {
                    if (xmlStrEqual(att->name, BAD_CAST "href")) {
                        xmlChar* hrefDump = xmlNodeGetContent(att->children);
                        if (hrefDump != nullptr) {
                            std::string href {reinterpret_cast<const char*>(hrefDump)};
                            std::string linkText {reinterpret_cast<const char*>(xmlNodeGetContent(curr))};
                            oss << (!linkText.starts_with("hashtag#")? "[" + linkText + "](" + href + ")": "#" + linkText.substr(8));
                        }
                        break;
                    }
                }
            }
        }
    }

    return oss.str();
}

[[nodiscard]] std::string extractImages(xmlNodeSetPtr images, const fs::path &imageExtractPath, const fs::path &dumpPath) {
    std::string imgPathStr {imageExtractPath.string()};

    std::ostringstream oss;
    for (int i = 0; i < (images? images->nodeNr: 0); i++) {
        std::string src, alt {"image"};
        for (xmlAttrPtr att = images->nodeTab[i]->properties; att != nullptr; att = att->next) {
            if (xmlStrEqual(att->name, BAD_CAST "src")) {
                const char* srcDump = reinterpret_cast<const char*>(xmlNodeGetContent(att->children));
                if (srcDump) src = urlDecode(srcDump);
            }

            else if (xmlStrEqual(att->name, BAD_CAST "alt")) {
                const char* altDump = reinterpret_cast<const char*>(xmlNodeGetContent(att->children));
                if (altDump) alt = altDump;
            }
        }

        // Write the image in markdown syntax
        if (!src.empty()) {
            fs::path srcPath {dumpPath / src};
            if (fs::exists(srcPath)) {
                src = imgPathStr + '.' + int2Str(i + 1, 2) + srcPath.extension().string();
                fs::copy(srcPath, src);
                fs::path relImgPath {fs::relative(src, imageExtractPath.parent_path().parent_path())};
                oss << "![" + alt + "](" + relImgPath.string() + ")" << "  \n";
            }
        }
    }

    return oss.str();
}

void extractContent(const fs::path &htmlPath, const fs::path &extractPath) {
    // Clear the extract path and recreate fresh
    const fs::path dumpPath {htmlPath.parent_path()};
    const fs::path imageExtractPath {extractPath / "images"};
    if (fs::exists(extractPath)) fs::remove_all(extractPath);
    fs::create_directories(imageExtractPath);

    // Parse the html file with no warnings
    htmlDocPtr doc = htmlReadFile(htmlPath.c_str(), nullptr, HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING);

    // Check if parsing was successful
    if (doc == nullptr)
        throw std::runtime_error("Failed to parse file");

    // Get the root element
    htmlNodePtr root = xmlDocGetRootElement(doc); 
    if (root == nullptr)
        throw std::runtime_error( "Empty document");

    // Extract all text inside ".update-text-component span span"
    xmlXPathContextPtr rootCtx = xmlXPathNewContext(doc);
    xmlXPathObjectPtr xpathResult = xmlXPathEvalExpression(reinterpret_cast<const xmlChar*>(
                "//div[contains(@class, 'fie-impression-container')]"), rootCtx);
    if (rootCtx == nullptr || xpathResult == nullptr || xpathResult->nodesetval == nullptr) {
        xmlXPathFreeContext(rootCtx);
        throw std::runtime_error("XPath Eval failed.");
    }

    // Print out the extracted node contents
    xmlNodeSetPtr nodes = xpathResult->nodesetval;
    for (int i = 0; i < nodes->nodeNr; i++) {
        htmlNodePtr contNode = nodes->nodeTab[i];

        // Create a corresponding .md file
        std::string fileName {int2Str(nodes->nodeNr - i)};
        std::ofstream ofs {extractPath / (fileName + ".md")};

        // Extract a local context from the .fie-impression-container
        xmlXPathContextPtr localCtx = xmlXPathNewContext(doc);
        localCtx->node = contNode;

        // Get the text div from the container Node
        xmlXPathObjectPtr textNodePath = xmlXPathEvalExpression(reinterpret_cast<const xmlChar*>(
                    ".//div[contains(@class, 'update-components-text')]/span[1]/span[1]"), localCtx);
        if (textNodePath && textNodePath->nodesetval) {
            ofs << extractTextWithFormatting(textNodePath->nodesetval->nodeTab[0]) << '\n';

            // Get the images inside the post, if found
            xmlXPathObjectPtr imgNodePath = xmlXPathEvalExpression(reinterpret_cast<const xmlChar*>(
                    ".//div[contains(@class, 'update-components-image')]//img"), localCtx);
            std::string imageMDPart {extractImages(imgNodePath->nodesetval, imageExtractPath / fileName, dumpPath)};
            if (!imageMDPart.empty()) ofs << '\n' << imageMDPart << '\n';

            // Cleanup img eval
            xmlXPathFreeObject(imgNodePath);
        }

        // Cleanup text, cont
        xmlXPathFreeObject(textNodePath);
        xmlXPathFreeContext(localCtx);

        // If nothing was written to file, delete it
        ofs.flush();
        if (ofs.tellp() == 0) {
            ofs.close();
            fs::remove(extractPath / (fileName + ".md"));
        }
    }

    // Cleanup
    xmlXPathFreeObject(xpathResult);
    xmlXPathFreeContext(rootCtx);
    xmlFreeDoc(doc);
    xmlCleanupParser();
}

int main() {
    const fs::path htmlPath = fs::path("dump") / "Activity _ Naresh Jagadeesan _ LinkedIn.htm";
    if (!fs::exists(htmlPath)) 
        throw std::runtime_error("HTML file doesn't exist: " + htmlPath.string());

    const fs::path extractPath {fs::current_path() / "content"};
    extractContent(htmlPath, extractPath);
}
