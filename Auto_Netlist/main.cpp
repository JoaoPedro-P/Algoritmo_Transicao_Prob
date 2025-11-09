#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <sstream>
#include <set>
#include <map> 
#include <unordered_map>
#include <unordered_set>
#include <regex>
#include <algorithm>
#include <memory>

using namespace std;

//Definição de cada uma das arestas do grafo
struct CircuitNode {
    int id = 0;
    string name; 
    string type; // Tipo simplificado (ex: "inpt", "and", "or", "out")
    
    // Conexões lógicas (grafo)
    vector<shared_ptr<CircuitNode>> fan_in_nodes;
    vector<shared_ptr<CircuitNode>> fan_out_nodes;

    // Armazenamento temporário durante o parsing
    vector<string> raw_input_signals;
    vector<string> raw_output_signals;
};

// Estrutura auxiliar para a poda de sinais não pareados (serve para remover os inputs que, por ventura não são conectáveis a aresta)
struct SignalInfo {
    string base_name;
    bool is_true_rail; 
    bool is_vector_bit = false; 
};

// Converte o tipo da instanciação do elemento lógico para o formato usado na netlist
string mapVerilogTypeToNetlistType(const string& verilogType) {
    if (verilogType.find("NAND") != string::npos) return "nand";
    if (verilogType.find("NOR") != string::npos) return "nor";
    if (verilogType.find("XOR") != string::npos) return "xor";
    if (verilogType.find("XNOR") != string::npos) return "xnor";
    
    if (verilogType.find("AND") != string::npos) return "and";
    if (verilogType.find("OR") != string::npos) return "or";
    if (verilogType.find("NOT") != string::npos) return "not";

    return "gate";
}

//Identifica se o elemento lido do arquivo é ou não um tipo básico. Essa verificação é importante para o processo de recursividade entre os módulos, pois, o processo de recursividade para quando são encontrados todos os elementos bases daquele módulo
bool isBaseCell(const string &instanceType) {
    static set<string> basePrefixes = {
        "THDR_AND", "THDR_OR", "THDR_XOR", "THDR_XNOR",
        "THDR_NAND", "THDR_NOT", "THDR_NOR"
    };
    for (const auto &prefix : basePrefixes) {
        if (instanceType.find(prefix) == 0)
            return true;
    }
    return false;
}

//Função para remoção dos espaços em brancos de uma string
void trim(string &s) {
    s.erase(s.begin(), find_if(s.begin(), s.end(), [](unsigned char ch) {
        return !isspace(ch);
    }));
    s.erase(find_if(s.rbegin(), s.rend(), [](unsigned char ch) {
        return !isspace(ch);
    }).base(), s.end());
}

//Identifica se há um espaço em branco na string
bool isWhitespaceLine(const string &line) {
    return line.find_first_not_of(" \t\r\n") == string::npos;
}

//Ordena as instâncias (elementos lógicos base) de maneira crescente por letra ou numeral
void sortInstances(vector<tuple<string, string, string>> &instances) {
    sort(instances.begin(), instances.end(), [](const auto &a, const auto &b) {
        auto extractParts = [](const string &s) {
            string clean;
            for (char c : s) {
                if (isalnum(c)) clean += c;
            }

            smatch match;
            regex re("([A-Za-z]+)(\\d+)");
            if (regex_match(clean, match, re)) {
                return make_pair(match[1].str(), stoi(match[2]));
            }

            return make_pair(clean, -1);
        };

        auto [prefixA, numA] = extractParts(get<0>(a));
        auto [prefixB, numB] = extractParts(get<0>(b));

        if (prefixA == prefixB)
            return numA < numB;
        return prefixA < prefixB;
    });
}

//Função para remover as informações de conexão da instanciação do módulo inferior advindas do módulo superior
unordered_map<string, string> extractPortMap(const string &instanceText) {
    unordered_map<string, string> portMap;
    smatch match;
    regex portRegex(R"(\.(\w+)\s*\(\s*([^)]+)\s*\))");
    auto begin = sregex_iterator(instanceText.begin(), instanceText.end(), portRegex);
    auto end = sregex_iterator();
    for (auto it = begin; it != end; ++it) {
        string port = (*it)[1];
        string signal = (*it)[2];
        portMap[port] = signal;
    }

    return portMap;
}

//Função para extração dos sinais de entrada e saída de cada módulo. Além de serem passados na instanciação do módulo, na descrição do próprio módulo os sinais também definidos. Essa função pega cada um deles
pair<unordered_set<string>, unordered_set<string>> getModuleIOs(const string &filename, const string &moduleName) {
    unordered_set<string> inputs, outputs;
    ifstream file(filename);
    if (!file) return {inputs, outputs};

    string line;
    bool inModule = false;
    while (getline(file, line)) {
        if (line.find("module " + moduleName) != string::npos) {
            inModule = true;
        } else if (inModule && line.find("endmodule") != string::npos) {
            break;
        } else if (inModule) {
            smatch match;
            regex ioRegex(R"((input|output)\s+([^;]+);)");
            if (regex_search(line, match, ioRegex)) {
                string type = match[1];
                string ports = match[2];
                stringstream ss(ports);
                string port;
                while (getline(ss, port, ',')) {
                    port.erase(remove_if(port.begin(), port.end(), ::isspace), port.end());
                    if (!port.empty()) {
                        if (type == "input") inputs.insert(port);
                        else outputs.insert(port);
                    }
                }
            }
        }
    }

    return {inputs, outputs};
}

//Função para extrair os nomes das entradas e saídas dos sinais e unificar os casos de vetores
string getBaseName(const string& signalName) {
    smatch match;

    // Regex para capturar nome base e índice de notação vetorial, ex: "Out[9]" -> "Out", 9
    regex vector_re(R"((.*)\[(\d+)\])");
    if (regex_match(signalName, match, vector_re) && match.size() > 2) {
        string prefix = match[1].str();
        int index = stoi(match[2].str());
        int pair_index = index / 2; // Agrupa por pares: [0,1]->0, [2,3]->1, etc.
        return prefix + to_string(pair_index);
    }
    
    
    regex rail_re(R"((.*)_[tf]$)");
    if (regex_search(signalName, match, rail_re)) {
        return match[1].str();
    }

    return signalName; // Retorna o nome original se nenhum padrão corresponder
}

//Função que identifica se um sinal é entrada ou saída do elemento lógico base. Essa verificação é necessária, pois, para um sinal ser entrada/saída de um elemento lógico, ele necessariamente precisa ser definido em par (afinal o circuito é em dual rail). 
SignalInfo parseSignalName(const string& signal) {
    SignalInfo info;
    smatch match;

    // Regex para entradas primárias VETORIAIS, ex: \A[1]~input_o
    regex vector_input_re(R"(\\(\w+)\[\d+\]~input_o)");
    // Regex para entradas primárias DUAL-RAIL, ex: \C_t~input_o
    regex dual_rail_input_re(R"(\\(\w+)_([tf])~input_o)");
    // Regex para saídas de portas DUAL-RAIL, ex: \muxOut0|Mux2|gMUX2|G0|out~0_combout
    regex gate_output_re(R"((.*)\|G([01])\|out~\d+_combout)");

    if (regex_search(signal, match, vector_input_re)) {
        info.base_name = match[1].str(); // Captura "A"
        info.is_vector_bit = true;
        info.is_true_rail = false;
    }
    else if (regex_search(signal, match, dual_rail_input_re)) {
        info.base_name = match[1].str(); // Captura "C"
        info.is_vector_bit = true; // Trata como um tipo de vetor para evitar poda incorreta
        info.is_true_rail = (match[2].str() == "t");
    }
    else if (regex_search(signal, match, gate_output_re)) {
        info.base_name = match[1].str(); 
        info.is_true_rail = (match[2].str() == "1");
    }
    else {
        info.base_name = signal;
        info.is_true_rail = false;
        info.is_vector_bit = false;
    }
    
    return info;
}

//Função para percorrer o módulo extraindo possíveis outros módulos ou células básicas
vector<tuple<string, string, string>> extractInnerInstances(const string& moduleContent) {
    vector<tuple<string, string, string>> instances;
    stringstream contentStream(moduleContent);
    string line;

    stringstream instanceTextStream;
    string currentInstanceType;
    string currentInstanceName;
    bool isInsideInstance = false;

    while (getline(contentStream, line)) {
        string trimmedLine = line;
        trim(trimmedLine);
        
        if (trimmedLine.empty() || trimmedLine.rfind("//", 0) == 0 || trimmedLine.rfind("module", 0) == 0 || trimmedLine.rfind("endmodule", 0) == 0 || trimmedLine.rfind("assign", 0) == 0 || trimmedLine.rfind("wire", 0) == 0) {
            continue;
        }

        if (!isInsideInstance && trimmedLine.rfind(".", 0) != 0 && trimmedLine.find('(') != string::npos) {
            isInsideInstance = true;
            stringstream type_ss(line);
            type_ss >> currentInstanceType >> currentInstanceName;
            if (!currentInstanceName.empty() && currentInstanceName.find('(') != string::npos) {
                currentInstanceName = currentInstanceName.substr(0, currentInstanceName.find('('));
            }
        }

        if (isInsideInstance) {
            instanceTextStream << line << "\n";

            if (trimmedLine.find(");") != string::npos) {
                if (!currentInstanceName.empty() && !currentInstanceType.empty()) {
                     instances.emplace_back(currentInstanceName, currentInstanceType, instanceTextStream.str());
                }
               
                isInsideInstance = false;
                instanceTextStream.str("");
                instanceTextStream.clear();
                currentInstanceType.clear();
                currentInstanceName.clear();
            }
        }
    }
    return instances;
}

//Função para extração de todos os módulos ou elementos lógicos base do arquivo de entrada
vector<tuple<string, string, string>> extractInstances(const string &filename) {
    ifstream file(filename);
    if (!file) {
        cerr << "Erro ao abrir o arquivo." << endl;
        return {};
    }

    string line;
    vector<tuple<string, string, string>> tempInstances;
    bool foundLastWire = false;
    size_t blankLines = 0;
    bool extracting = false;

    stringstream instanceContent;
    string instanceType, instanceName;

    while (getline(file, line)) { 
        if (line.find("// Location:") != string::npos) { 
            break;
        }

        if (!extracting) {
            if (foundLastWire) {
                if (isWhitespaceLine(line)) {
                    blankLines++;
                    if (blankLines >= 2) { 
                        extracting = true;
                    }
                } else {
                    blankLines = 0;
                }
            } else if (line.find("wire ") != string::npos) { 
                foundLastWire = true;
                blankLines = 0;
            }
        } else {
            if (line.find('(') != string::npos && instanceType.empty()) {
                stringstream ss(line);
                ss >> instanceType >> instanceName;
                instanceName = instanceName.substr(0, instanceName.find('('));
                trim(instanceName);
                trim(instanceType);
            }

            if (!isWhitespaceLine(line)) {
                instanceContent << line << endl;
            }

            if (line.find(");") != string::npos) {
                if (!instanceName.empty() && !instanceType.empty()) {
                    tempInstances.emplace_back(instanceName, instanceType, instanceContent.str()); 
                }
                instanceContent.str("");
                instanceContent.clear();
                instanceType.clear();
                instanceName.clear();
            }
        }
    }
    
    sortInstances(tempInstances);
    file.close();
    return tempInstances;
}


//Função para extrair o contexto de cada módulo. O contexto é a definição do módulo no módulo o qual ele é chamado. Ex: modulo topo instância o módulo B. Essa instanciação contém todos os sinais de entrada e saída do módulo B. Então essa função busca toda instanciação do módulo para ter noção dos sinais de entrada e saída
string extractModuleContent(const string &filename, const string &moduleName) {
    ifstream file(filename);
    if (!file) {
        cerr << "Erro ao abrir o arquivo." << endl;
        return "";
    }

    string line;
    stringstream moduleContent;
    bool inModule = false;
    while (getline(file, line)) {
        if (!inModule) {
            if (line.find("module " + moduleName) != string::npos) {
                inModule = true;
                moduleContent << line << endl;
            }
        } else {
            moduleContent << line << endl;
            if (line.find("endmodule") != string::npos) {
                break;
            }
        }
    }

    file.close();
    return moduleContent.str();
}


//Função recurssiva para percorrer os módulos até encontrar as células base
void flattenAndResolve(const string& moduleType,
    const string& instancePrefix,
    const unordered_map<string, string>& parentConnections,
    const string& vo_filename,
    vector<pair<string, string>>& flattenedInstances
) {
    string moduleContent = extractModuleContent(vo_filename, moduleType);
    if (moduleContent.empty()) {
        cerr << "Aviso: Não foi possível encontrar a definição para o módulo " << moduleType << endl;
        return;
    }

    vector<tuple<string, string, string>> innerInstances = extractInnerInstances(moduleContent);
    sortInstances(innerInstances);
    for (const auto& [instName, instType, instText] : innerInstances) {
        string globalInstName = instancePrefix + instName;
        auto localPortMap = extractPortMap(instText);
        unordered_map<string, string> childConnections;

        for (const auto& [port, wire] : localPortMap) {
            if (parentConnections.count(wire)) {
                childConnections[port] = parentConnections.at(wire);
            } else {
                string tempWire = wire;
                if (!tempWire.empty() && tempWire.rfind("\\", 0) == 0) {
                    tempWire = tempWire.substr(1);
                }
                childConnections[port] = "\\" + instancePrefix + tempWire;
            }
        }

        if (isBaseCell(instType)) {
            stringstream newInstanceText;
            newInstanceText << instType << " " << globalInstName << " (\n";
            size_t count = 0;
            map<string, string> sortedChildConnections(childConnections.begin(), childConnections.end());
            for (const auto& [port, signal] : sortedChildConnections) {
                 newInstanceText << "\t." << port << "(" << signal << ")" << (++count == sortedChildConnections.size() ? "" : ",") << "\n";
            }
            newInstanceText << ");";
            flattenedInstances.push_back({instType, newInstanceText.str()});
        } else {
            flattenAndResolve(instType, globalInstName + "|", childConnections, vo_filename, flattenedInstances);
        }
    }
}

//Função para montar o arquivo de saída output.txt organizado como: módulo topo -> inputs/outputs -> módulos intermediários e de quem eles são instanciados
void resolveModules(vector<pair<string, string>> &instances, const string &filename) {
    for (auto &instance : instances) {
        if (!isBaseCell(instance.first)) {
            string content = extractModuleContent(filename, instance.first);
            if (content.empty()) continue;

            auto [inputs, outputs] = getModuleIOs(filename, instance.first);
            auto portMap = extractPortMap(instance.second);

            stringstream resolvedHeader;
            resolvedHeader << "Instância: " << instance.first << endl;

            resolvedHeader << "inputs:\n";
            for (const auto &in : inputs) {
                if (portMap.count(in)) {
                    resolvedHeader << in << " = " << portMap[in] << endl;
                }
            }

            resolvedHeader << "\noutputs:\n";
            for (const auto &out : outputs) {
                if (portMap.count(out)) {
                    resolvedHeader << out << " = " << portMap[out] << endl;
                }
            }

            resolvedHeader << endl;
            stringstream ss(content);
            string line;
            vector<pair<string, string>> subInstances;
            vector<tuple<string, string, string>> tempInstances;

            while (getline(ss, line)) {
                if (line.find("module ") != string::npos)
                    continue;
                if (line.find('(') != string::npos && line.find("wire ") == string::npos) {
                    stringstream ls(line);
                    string type, name;
                    ls >> type >> name;

                    if (isBaseCell(type)) {
                        string fullInstance = line;
                        while (line.find(");") == string::npos && getline(ss, line)) {
                            fullInstance += "\n" + line;
                        }
                        tempInstances.emplace_back(name, type, fullInstance);
                    }
                }
            }

            sortInstances(tempInstances);
            for (const auto &[name, type, fullInstance] : tempInstances) {
                subInstances.push_back({type, fullInstance});
            }

            stringstream resolvedContent;
            for (const auto &sub : subInstances) {
                resolvedContent << "// Instância resolvida de " << sub.first << endl;
                resolvedContent << sub.second << endl;
            }

            instance.second = resolvedHeader.str() + resolvedContent.str();
        }
    }
}

//Função para remoção das informações do módulo topo: nome do módulo, inputs e outputs
tuple<string, unordered_set<string>, unordered_set<string>> getTopModuleHeaderInfo(const string &filename) {
    ifstream file(filename);
    if (!file) {
        cerr << "Erro ao abrir o arquivo para header do módulo topo." << endl;
        return {};
    }

    string line;
    string moduleName;
    unordered_set<string> inputs, outputs;
    bool foundTimescale = false;
    bool inModuleHeader = false;
    bool headerEnded = false;

    while (getline(file, line)) {
        if (!foundTimescale && line.find("`timescale") != string::npos) {
            foundTimescale = true;
            continue;
        }

        if (foundTimescale && !inModuleHeader && line.find("module ") != string::npos) {
            smatch match;
            regex moduleRegex(R"(module\s+(\w+))");
            if (regex_search(line, match, moduleRegex)) {
                moduleName = match[1];
                inModuleHeader = true;
            }
        }

        if (inModuleHeader && line.find(");") != string::npos) {
            headerEnded = true;
        }

        if (headerEnded) {
            smatch match;
            regex ioRegex(R"((input|output)\s+(.*?);)");
            if (regex_search(line, match, ioRegex)) {
                string type = match[1];
                string signalList = match[2];
                signalList = regex_replace(signalList, regex(R"(\[[^\]]+\])"), "");
                stringstream ss(signalList);
                string signal;
                while (getline(ss, signal, ',')) {
                    signal.erase(remove_if(signal.begin(), signal.end(), ::isspace), signal.end());
                    if (!signal.empty()) {
                        if (type == "input") inputs.insert(signal);
                        else outputs.insert(signal);
                    }
                }
            }

            if (line.find("// Design Ports Information") != string::npos)
                break;
        }
    }

    return {moduleName, inputs, outputs};
}

//Função para extrair do arquivo de entrada a quais saídas dos elementos que compõe o circuito as saídas se conectam
unordered_map<string, string> extractOutputConnections(const string &filename) {
    unordered_map<string, string> connections;
    ifstream file(filename);
    if (!file) {
        cerr << "Erro ao abrir o arquivo para extrair conexões de saída." << endl;
        return connections;
    }

    string line;
    string instanceContent;
    bool inOutputInstance = false;
    while (getline(file, line)) {
        if (line.find("fiftyfivenm_io_obuf") != string::npos) {
            inOutputInstance = true;
            instanceContent.clear();
        }

        if (inOutputInstance) {
            instanceContent.append(line + "\n");
            if (line.find(");") != string::npos) {
                inOutputInstance = false;
                smatch o_match, i_match;
                regex o_regex(R"(\.o\s*\(\s*([^\s)]+)\s*\))");
                regex i_regex(R"(\.i\s*\(\s*([^\s)]+)\s*\))");
                string outputName, inputWire;

                if (regex_search(instanceContent, o_match, o_regex) && o_match.size() > 1) {
                    outputName = o_match[1].str();
                }

                if (regex_search(instanceContent, i_match, i_regex) && i_match.size() > 1) {
                    inputWire = i_match[1].str();
                }

                if (!outputName.empty() && !inputWire.empty()) {
                    connections[outputName] = inputWire;
                }
            }
        }
    }

    file.close();
    return connections;
}

//Função que gera o arquivo intermediário, ou seja, que extrai todos os elementos lógicos base do arquivo de entrada, conexões de entrada e saída e nomes.
    bool generateIntermediateFile(const string& vo_filename, const string& output_filename) {
    cout << "Lendo arquivo de entrada: " << vo_filename << endl;
    auto [topModuleName, topInputs, topOutputs] = getTopModuleHeaderInfo(vo_filename);
    if (topModuleName.empty()) {
        cerr << "Não foi possível extrair o módulo topo." << endl;
        return false;
    }
    auto outputConnections = extractOutputConnections(vo_filename);
    vector<pair<string, string>> allFlattenedInstances;
    vector<tuple<string, string, string>> topLevelInstances = extractInstances(vo_filename);
    for (const auto& [instName, instType, instText] : topLevelInstances) {
        if (isBaseCell(instType)) {
            allFlattenedInstances.push_back({instType, instText});
        } else {
            auto parentConnections = extractPortMap(instText);
            flattenAndResolve(instType, instName + "|", parentConnections, vo_filename, allFlattenedInstances);
        }
    }

    ofstream outputFile(output_filename);
    if (!outputFile) {
        cerr << "Erro ao criar o arquivo intermediário de saída." << endl;
        return false;
    }

    outputFile << "Instância topo da hierarquia: " << topModuleName << endl;
    outputFile << "inputs:\n";
    vector<string> sortedInputs(topInputs.begin(), topInputs.end());
    sort(sortedInputs.begin(), sortedInputs.end());
    for (const auto &in : sortedInputs) {
        outputFile << in << " = " << in << endl;
    }

    outputFile << "\noutputs:\n";
    vector<string> sortedOutputs;
    for(const auto& pair : outputConnections) {
        sortedOutputs.push_back(pair.first);
    }
    sort(sortedOutputs.begin(), sortedOutputs.end(), [](const string &a, const string &b) {
        regex re(R"((.*?)\[(\d+)\]|(.+))");
        smatch matchA, matchB;
        string baseA, baseB;
        int indexA = -1, indexB = -1;
        if (regex_match(a, matchA, re)) {
            baseA = matchA[1].matched ? matchA[1].str() : matchA[3].str();
             if (matchA[2].matched) indexA = stoi(matchA[2]);
        }
        if (regex_match(b, matchB, re)) {
            baseB = matchB[1].matched ? matchB[1].str() : matchB[3].str();
            if (matchB[2].matched) indexB = stoi(matchB[2]);
        }
        if (baseA != baseB) return baseA < baseB;
        return indexA < indexB;
    });
    for (const auto &outName : sortedOutputs) {
        outputFile << outName << " = " << outputConnections[outName] << endl;
    }

    outputFile << endl;

    for (const auto &pair : allFlattenedInstances) {
        outputFile << "// Instância resolvida de " << pair.first << endl;
        outputFile << pair.second << endl;
    }

    outputFile.close();
    cout << "Arquivo intermediário '" << output_filename << "' gerado com sucesso." << endl;
    return true;
}

//Função que recebe como entrada o arquivo intermediário (output.txt) e o transforma no formato de netlist alvo
void generateSimplifiedNetlist(const string& inputFilename, const string& outputFilename) {
    ifstream inputFile(inputFilename);
    if (!inputFile) {
        cerr << "Erro: Não foi possível abrir o arquivo de entrada " << inputFilename << endl;
        return;
    }

    // --- FASE 1: PARSING E CRIAÇÃO DE NÓS ---
    map<string, shared_ptr<CircuitNode>> name_to_node;
    unordered_map<string, shared_ptr<CircuitNode>> signal_to_source_node;

    string line;
    string current_section = "";
    string instance_block;
    bool in_instance_block = false;
    
    map<string, string> local_to_global_signal_map;
    map<string, string> submodule_output_map; 

    while (getline(inputFile, line)) {
        if (line.rfind("Instância: ", 0) == 0) {
            current_section = "submodule_header";
            local_to_global_signal_map.clear();
            submodule_output_map.clear();
            continue;
        }
        if (line.rfind("inputs:", 0) == 0) {
            current_section = (current_section == "submodule_header" || current_section == "submodule_outputs") ? "submodule_inputs" : "toplevel_inputs";
            continue;
        }
        if (line.rfind("outputs:", 0) == 0) {
            current_section = (current_section == "submodule_header" || current_section == "submodule_inputs") ? "submodule_outputs" : "toplevel_outputs";
            continue;
        }
        if (line.rfind("// Instância resolvida", 0) == 0) {
             current_section = "gates";
        }

        if ((current_section == "toplevel_inputs" || current_section == "toplevel_outputs") && line.empty()) {
            current_section = "gates";
            continue;
        }
        
        if (current_section == "toplevel_inputs") {
            stringstream ss(line);
            string name, eq, val;
            ss >> name >> eq >> val;
            string baseName = getBaseName(name);
            if (name_to_node.find(baseName) == name_to_node.end()) {
                auto node = make_shared<CircuitNode>();
                node->name = baseName;
                node->type = "inpt";
                name_to_node[baseName] = node;
            }
        } 
        else if (current_section == "toplevel_outputs" || current_section == "submodule_outputs") {
            stringstream ss(line);
            string local_name, eq, global_signal;
            ss >> local_name >> eq >> global_signal;
            trim(local_name);
            trim(global_signal);
            if(current_section == "toplevel_outputs") {
                string baseName = getBaseName(local_name);
                if (name_to_node.find(baseName) == name_to_node.end()) {
                    auto node = make_shared<CircuitNode>();
                    node->name = baseName;
                    node->type = "out";
                    name_to_node[baseName] = node;
                }
                name_to_node[baseName]->raw_input_signals.push_back(global_signal);
            } else {
                submodule_output_map[local_name] = global_signal;
            }
        }
        else if (current_section == "submodule_inputs") {
            stringstream ss(line);
            string local_name, eq, global_signal;
            ss >> local_name >> eq >> global_signal;
            trim(local_name);
            trim(global_signal);
            local_to_global_signal_map[local_name] = global_signal;
        }
        else if (current_section == "gates") {
            if (line.find('(') != string::npos && line.find_first_not_of(" \t") != string::npos && !in_instance_block) {
                in_instance_block = true;
                instance_block = line + "\n";
            } else if (in_instance_block) {
                instance_block += line + "\n";
                if (line.find(");") != string::npos) {
                    in_instance_block = false;
                    
                    string type, name;
                    
                    stringstream ss(instance_block);
                    string first_line;
                    getline(ss, first_line);
                    
                    size_t type_end_pos = first_line.find_first_of(" \t");
                    type = first_line.substr(0, type_end_pos);

                    size_t name_start_pos = first_line.find_first_not_of(" \t", type_end_pos);
                    size_t paren_pos = first_line.find('(');
                    name = first_line.substr(name_start_pos, paren_pos - name_start_pos);
                    trim(name);
                    
                    if (name_to_node.find(name) == name_to_node.end()) {
                        auto node = make_shared<CircuitNode>();
                        node->name = name;
                        node->type = mapVerilogTypeToNetlistType(type);
                        name_to_node[name] = node;
                    }
                    auto& node = name_to_node[name];
                    regex port_regex(R"(\.\s*(\w+)\s*\(([^)]*)\))");
                    auto words_begin = sregex_iterator(instance_block.begin(), instance_block.end(), port_regex);
                    auto words_end = sregex_iterator();
                    for (sregex_iterator i = words_begin; i != words_end; ++i) {
                        string port_name = (*i)[1].str();
                        string signal = (*i)[2].str();
                        if (port_name == "comb" || port_name == "comb1" || port_name == "comb2") continue;
                        trim(signal);
                        if (signal.empty() || signal.find("dev") == 0) continue;
                        if (submodule_output_map.count(signal)) {
                             string global_signal = submodule_output_map[signal];
                             node->raw_output_signals.push_back(global_signal);
                             signal_to_source_node[global_signal] = node;
                        } else {
                            string resolved_signal = signal;
                            if(local_to_global_signal_map.count(signal)) {
                                resolved_signal = local_to_global_signal_map[signal];
                            }
                            
                            if (resolved_signal.find(name) != string::npos && resolved_signal.rfind("\\", 0) == 0) { 
                                node->raw_output_signals.push_back(resolved_signal);
                                signal_to_source_node[resolved_signal] = node;
                            } else { 
                                node->raw_input_signals.push_back(resolved_signal);
                                SignalInfo info = parseSignalName(resolved_signal);
                                if (name_to_node.count(info.base_name) && name_to_node[info.base_name]->type == "inpt") {
                                    signal_to_source_node[resolved_signal] = name_to_node[info.base_name];
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // --- FASE 1.5: PODA DE SINAIS DE ENTRADA NÃO PAREADOS ---
    for (auto const& [name, node] : name_to_node) {
        if (node->type == "inpt" || node->type == "out") continue;
        map<string, int> pair_counter; 
        for (const auto& signal : node->raw_input_signals) {
            SignalInfo info = parseSignalName(signal);
            if (!info.is_vector_bit) {
                pair_counter[info.base_name] |= (info.is_true_rail ? 2 : 1);
            }
        }

        vector<string> pruned_inputs;
        for (const auto& signal : node->raw_input_signals) {
             SignalInfo info = parseSignalName(signal);
             if (info.is_vector_bit || (pair_counter.count(info.base_name) && pair_counter[info.base_name] == 3)) {
                 pruned_inputs.push_back(signal);
             }
        }
        node->raw_input_signals = pruned_inputs;
    }

    // --- FASE 2: CONSTRUÇÃO DAS CONEXÕES (GRAFO) ---
    for (auto const& [name, node] : name_to_node) {
        for (const auto& input_signal : node->raw_input_signals) {
             if (signal_to_source_node.count(input_signal)) {
                auto source_node = signal_to_source_node[input_signal];
                if (find(node->fan_in_nodes.begin(), node->fan_in_nodes.end(), source_node) == node->fan_in_nodes.end()) {
                    node->fan_in_nodes.push_back(source_node);
                }
                if (find(source_node->fan_out_nodes.begin(), source_node->fan_out_nodes.end(), node) == source_node->fan_out_nodes.end()) {
                    source_node->fan_out_nodes.push_back(node);
                }
            }
        }
    }

    // --- FASE 3: GERAÇÃO DO ARQUIVO DE SAÍDA ---
    ofstream outputFile(outputFilename);
    if (!outputFile) { cerr << "Erro: Não foi possível criar o arquivo de saída " << outputFilename << endl; return;
    }

    vector<shared_ptr<CircuitNode>> inputs, gates, outputs;
    for (auto const& [name, node] : name_to_node) {
        if (node->type == "inpt") inputs.push_back(node);
        else if (node->type == "out") outputs.push_back(node);
        else gates.push_back(node);
    }
    
    auto natural_sort = [](const shared_ptr<CircuitNode>& a, const shared_ptr<CircuitNode>& b) {
        auto extract_parts = [](const string &s) -> pair<string, int> {
            smatch match;
            
            regex re(R"((.*?)(\d+)$)");
            if (regex_search(s, match, re) && match.size() > 2) {
                return {match[1].str(), stoi(match[2].str())};
            }
            return {s, -1};
        };
        
        pair<string, int> parts_a = extract_parts(a->name);
        pair<string, int> parts_b = extract_parts(b->name);
        
        if (parts_a.first != parts_b.first) {
            return parts_a.first < parts_b.first;
        }
        return parts_a.second < parts_b.second;
    };

    sort(inputs.begin(), inputs.end(), natural_sort);
    sort(gates.begin(), gates.end(), natural_sort);
    sort(outputs.begin(), outputs.end(), natural_sort);

    int current_id = 1;
    for (auto& node : inputs) { node->id = current_id++;
    }
    for (auto& node : gates) { node->id = current_id++;
    }
    for (auto& node : outputs) { node->id = current_id++;
    }

    auto sort_by_id = [](const auto& a, const auto& b){ return a->id < b->id;
    };
    sort(inputs.begin(), inputs.end(), sort_by_id);
    sort(gates.begin(), gates.end(), sort_by_id);
    sort(outputs.begin(), outputs.end(), sort_by_id);
    for (auto& node : inputs) {
        outputFile << node->id << " " << node->type << " " << node->fan_out_nodes.size() << " " << node->fan_in_nodes.size() << " //" << node->name << endl;
    }
    for (auto& node : gates) {
        outputFile << node->id << " " << node->type << " " << node->fan_out_nodes.size() << " " << node->fan_in_nodes.size() << " //" << node->name << endl;
        outputFile << "\t";
        sort(node->fan_in_nodes.begin(), node->fan_in_nodes.end(), [](const auto& a, const auto& b){ return a->id < b->id; });
        for (const auto& input_node : node->fan_in_nodes) { outputFile << input_node->id << " ";
        }
        outputFile << endl;
    }
    for (auto& node : outputs) {
        outputFile << node->id << " " << node->type << " 0 " << node->fan_in_nodes.size()<< " //" << node->name << endl;
        outputFile << "\t";
        sort(node->fan_in_nodes.begin(), node->fan_in_nodes.end(), [](const auto& a, const auto& b){ return a->id < b->id; });
        for (const auto& input_node : node->fan_in_nodes) { outputFile << input_node->id << " ";
        }
        outputFile << endl;
    }
    cout << "Netlist simplificada gerada com sucesso em " << outputFilename << endl;
}



int main(int argc, char* argv[]) {

    string vo_filename = "ULA.vo";
    string intermediate_file = "output.txt";
    string final_netlist_file = "netlist_final.txt";

    cout << "--- Etapa 1: Gerando arquivo intermediário ---" << endl;
    if (!generateIntermediateFile(vo_filename, intermediate_file)) {
        cerr << "Falha ao gerar o arquivo intermediário. Abortando." << endl;
        return 1;
    }
    cout << "--------------------------------------------" << endl << endl;

    cout << "--- Etapa 2: Gerando netlist final ---" << endl;
    generateSimplifiedNetlist(intermediate_file, final_netlist_file);
    cout << "------------------------------------" << endl;

    return 0;
}