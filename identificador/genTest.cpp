#include <bits/stdc++.h>

using namespace std;

// Estrutura para armazenar informações de um elemento da netlist
struct Element {
    int id;
    string type;
    vector<float> connections; // Conexões gerais (entradas ou saídas)
    vector<float> selectors;   // Conexões específicas, como seletoras de mux ou op de sum_sub
    double prob_0;           // Probabilidade de ocorrer nível lógico 0
    double prob_1;           // Probabilidade de ocorrer nível lógico 1
    double carry_out_prob_0; // Probabilidade de carry_out ser 0 (somente para sum_sub)
    double carry_out_prob_1; // Probabilidade de carry_out ser 1 (somente para sum_sub)
};





// Função para ler o arquivo e construir o grafo da netlist
void parseNetlist(const string& filename, map<int, Element>& netlist) {
    ifstream file(filename);

    if (!file.is_open()) {
        cerr << "Error: Could not open file " << filename << endl;
        return;
    }

    string line;
    while (getline(file, line)) {
        if (line.empty()) {
            continue; // Ignorar linhas em branco
        }

        stringstream ss(line);
        Element elem;
        int num_outputs, num_inputs;
        ss >> elem.id >> elem.type >> num_outputs >> num_inputs;

        // Inicializar probabilidades para "inpt"
        if (elem.type == "inpt") {
            elem.prob_0 = 0.25; // Probabilidade padrão para 0
            elem.prob_1 = 0.25; // Probabilidade padrão para 1
        } else {
            elem.prob_0 = 1.0; // Valores iniciais genéricos para outros tipos
            elem.prob_1 = 1.0;
        }

        // Ler conexões para outros tipos
        if (elem.type == "sum_sub") {
            // Ler três linhas específicas para sum_sub
            // Primeira linha: entradas
            getline(file, line);
            stringstream inputs_stream(line);
            float input;
            while (inputs_stream >> input) {
                elem.connections.push_back(input);
            }

            // Segunda linha: carry_in
            getline(file, line);
            stringstream carry_stream(line);
            float carry_in;
            carry_stream >> carry_in;
            elem.connections.push_back(carry_in);

            // Terceira linha: operando
            getline(file, line);
            stringstream op_stream(line);
            int op;
            op_stream >> op;
            elem.selectors.push_back(op);
        } else {
            if (elem.type == "mux") {
                getline(file, line);
                stringstream conn_stream(line);
                float conn;
                while (conn_stream >> conn) {
                    elem.connections.push_back(conn);
                }
                // Ler conexões específicas para mux
                getline(file, line);
                stringstream sel_stream(line);
                while (sel_stream >> conn) {
                    elem.selectors.push_back(conn);
                }
            } else {
                for (int i = elem.type == "out" ? 0 : 1; i < num_inputs; ++i) {
                    getline(file, line);
                    stringstream conn_stream(line);
                    float conn;
                    while (conn_stream >> conn) {
                        elem.connections.push_back(conn);
                    }
                }
            }
        }

        netlist[elem.id] = elem;
    }

    file.close();
}





// Função para calcular probabilidades para portas lógicas e elementos especiais
void calculateElementProbability(map<int, Element>& netlist, Element& elem) {
    for (auto& [id, elem] : netlist) {
        if (elem.connections.size() >= 1) {
            const Element& input1 = netlist.at(elem.connections[0]);
            if (elem.type == "not") {
                elem.prob_0 = input1.prob_1;
                elem.prob_1 = input1.prob_0;
            } else if (elem.connections.size() >= 2) {
                const Element& input2 = netlist.at(elem.connections[1]);
                if (elem.type == "and") {
                    elem.prob_1 = input1.prob_1 * input2.prob_1;
                    elem.prob_0 = input1.prob_0 + input2.prob_0 - (input1.prob_0 * input2.prob_0);
                } else if (elem.type == "or") {
                    elem.prob_1 = input1.prob_1 + input2.prob_1 - (input1.prob_1 * input2.prob_1);
                    elem.prob_0 = input1.prob_0 * input2.prob_0;
                } else if (elem.type == "xor") {
                    elem.prob_0 = input1.prob_0 * input2.prob_0 + input1.prob_1 * input2.prob_1;
                    elem.prob_1 = input1.prob_0 * input2.prob_1 + input1.prob_1 * input2.prob_0;
                } else if (elem.type == "nand") {
                    elem.prob_0 = input1.prob_1 * input2.prob_1;
                    elem.prob_1 = input1.prob_0 + input2.prob_0 - (input1.prob_0 * input2.prob_0);
                } else if (elem.type == "nor") {
                    elem.prob_0 = input1.prob_1 + input2.prob_1 - (input1.prob_1 * input2.prob_1);
                    elem.prob_1 = input1.prob_0 * input2.prob_0;
                } else if (elem.type == "xnor") {
                    elem.prob_0 = input1.prob_0 * input2.prob_1 + input1.prob_1 * input2.prob_0;
                    elem.prob_1 = input1.prob_0 * input2.prob_0 + input1.prob_1 * input2.prob_1;
                }
            }

            if (elem.type == "mux" && elem.connections.size() >= 2 && elem.selectors.size() >= 1) {
                int in_a = elem.connections[0];
                int in_b = elem.connections[1];
                int sel_c = elem.selectors[0];

                const Element& input_a = netlist.at(in_a);
                const Element& input_b = netlist.at(in_b);
                const Element& selector_c = netlist.at(sel_c);

                // Cálculo da porta NOT para ~C
                double prob_c_0 = selector_c.prob_1;
                double prob_c_1 = selector_c.prob_0;

                // Cálculo das portas AND para AC e B~C
                double prob_ac_1 = input_a.prob_1 * selector_c.prob_1;
                double prob_ac_0 = input_a.prob_0 + selector_c.prob_0 - (input_a.prob_0 * selector_c.prob_0);

                double prob_bnc_1 = input_b.prob_1 * prob_c_0;
                double prob_bnc_0 = input_b.prob_0 + prob_c_1 - (input_b.prob_0 * prob_c_1);

                // Cálculo da porta OR para AC + B~C
                elem.prob_1 = prob_ac_1 + prob_bnc_1 - (prob_ac_1 * prob_bnc_1);
                elem.prob_0 = prob_ac_0 * prob_bnc_0;
            }

            if (elem.type == "sum_sub" && elem.connections.size() >= 3 && elem.selectors.size() >= 1) {
                int in_a = elem.connections[0];  // Entrada A
                int in_b = elem.connections[1];  // Entrada B
                int in_cin = elem.connections[2];  // Carry-in
                int op = elem.selectors[0];  // Operador (Op)

                const Element& input_a = netlist.at(in_a);
                const Element& input_b = netlist.at(in_b);
                const Element& input_cin = netlist.at(in_cin);
                const Element& input_op = netlist.at(op);

                // Probabilidades NOT para ~A, ~B, ~Cin e ~Op
                double prob_a_0 = input_a.prob_0;
                double prob_a_1 = input_a.prob_1;

                double prob_b_0 = input_b.prob_0;
                double prob_b_1 = input_b.prob_1;

                double prob_cin_0 = input_cin.prob_0;
                double prob_cin_1 = input_cin.prob_1;

                double prob_op_0 = input_op.prob_0;
                double prob_op_1 = input_op.prob_1;

                // Cálculo para o Termo 1: A AND ~B AND ~Cin
                double prob_not_b_0 = prob_b_1;
                double prob_not_b_1 = prob_b_0;

                double prob_and_a_not_b_1 = prob_a_1 * prob_not_b_1;
                double prob_and_a_not_b_0 = prob_a_0 + prob_not_b_0 - (prob_a_0 * prob_not_b_0);

                double prob_t1_1 = prob_and_a_not_b_1 * prob_cin_1;
                double prob_t1_0 = prob_and_a_not_b_0 + prob_cin_0 - (prob_and_a_not_b_0 * prob_cin_0);

                // Cálculo para o Termo 2: A AND B AND Cin
                double prob_and_a_b_1 = prob_a_1 * prob_b_1;
                double prob_and_a_b_0 = prob_a_0 + prob_b_0 - (prob_a_0 * prob_b_0);

                double prob_t2_1 = prob_and_a_b_1 * prob_cin_1;
                double prob_t2_0 = prob_and_a_b_0 + prob_cin_0 - (prob_and_a_b_0 * prob_cin_0);

                // Cálculo para o Termo 3: ~A AND ~B AND Cin
                double prob_not_a_0 = prob_a_1;
                double prob_not_a_1 = prob_a_0;

                double prob_and_not_a_not_b_1 = prob_not_a_1 * prob_not_b_1;
                double prob_and_not_a_not_b_0 = prob_not_a_0 + prob_not_b_0 - (prob_not_a_0 * prob_not_b_0);

                double prob_t3_1 = prob_and_not_a_not_b_1 * prob_cin_1;
                double prob_t3_0 = prob_and_not_a_not_b_0 + prob_cin_0 - (prob_and_not_a_not_b_0 * prob_cin_0);

                // Cálculo para o Termo 4: ~A AND B AND ~Cin
                double prob_and_not_a_b_1 = prob_not_a_1 * prob_b_1;
                double prob_and_not_a_b_0 = prob_not_a_0 + prob_b_0 - (prob_not_a_0 * prob_b_0);

                double prob_t4_1 = prob_and_not_a_b_1 * prob_cin_1;
                double prob_t4_0 = prob_and_not_a_b_0 + prob_cin_0 - (prob_and_not_a_b_0 * prob_cin_0);

                // Combinação dos Termos 1 e 2 com OR
                double prob_partial1_1 = prob_t1_1 + prob_t2_1 - (prob_t1_1 * prob_t2_1);
                double prob_partial1_0 = prob_t1_0 * prob_t2_0;

                // Combinação dos Termos 3 e 4 com OR
                double prob_partial2_1 = prob_t3_1 + prob_t4_1 - (prob_t3_1 * prob_t4_1);
                double prob_partial2_0 = prob_t3_0 * prob_t4_0;

                // Resultado final para a saída principal (out)
                elem.prob_1 = prob_partial1_1 + prob_partial2_1 - (prob_partial1_1 * prob_partial2_1);
                elem.prob_0 = prob_partial1_0 * prob_partial2_0;

                // Cálculos intermediários para o carry-out
                // Termo 1: B AND Cin
                double prob_and_b_cin_1 = prob_b_1 * prob_cin_1;
                double prob_and_b_cin_0 = prob_b_0 + prob_cin_0 - (prob_b_0 * prob_cin_0);

                // Termo 2: ~Op AND A AND Cin
                double prob_not_op_1 = prob_op_0;
                double prob_not_op_0 = prob_op_1;

                double prob_and_not_op_a_1 = prob_not_op_1 * prob_a_1;
                double prob_and_not_op_a_0 = prob_not_op_0 + prob_a_0 - (prob_not_op_0 * prob_a_0);

                double prob_term2_1 = prob_and_not_op_a_1 * prob_cin_1;
                double prob_term2_0 = prob_and_not_op_a_0 + prob_cin_0 - (prob_and_not_op_a_0 * prob_cin_0);

                // Termo 3: Op AND ~A AND Cin
                double prob_and_op_not_a_1 = prob_op_1 * prob_not_a_1;
                double prob_and_op_not_a_0 = prob_op_0 + prob_not_a_0 - (prob_op_0 * prob_not_a_0);

                double prob_term3_1 = prob_and_op_not_a_1 * prob_cin_1;
                double prob_term3_0 = prob_and_op_not_a_0 + prob_cin_0 - (prob_and_op_not_a_0 * prob_cin_0);

                // Combinação dos Termos 1 e 2 com OR
                double prob_partial_ct1_ct2_1 = prob_and_b_cin_1 + prob_term2_1 - (prob_and_b_cin_1 * prob_term2_1);
                double prob_partial_ct1_ct2_0 = prob_and_b_cin_0 * prob_term2_0;

                // Combinação do resultado com Termo 3 com OR
                double prob_partial_ct1_1 = prob_partial_ct1_ct2_1 + prob_term3_1 - (prob_partial_ct1_ct2_1 * prob_term3_1);
                double prob_partial_ct1_0 = prob_partial_ct1_ct2_0 * prob_term3_0;

                // Termo 4: Op AND ~A AND B
                double prob_and_op_not_a_1_step1 = prob_op_1 * prob_not_a_1;
                double prob_and_op_not_a_0_step1 = prob_op_0 + prob_not_a_0 - (prob_op_0 * prob_not_a_0);

                double prob_term4_1 = prob_and_op_not_a_1_step1 * prob_b_1;
                double prob_term4_0 = prob_and_op_not_a_0_step1 + prob_b_0 - (prob_and_op_not_a_0_step1 * prob_b_0);

                // Termo 5: ~Op AND A AND B
                double prob_and_not_op_a_1_step1 = prob_not_op_1 * prob_a_1;
                double prob_and_not_op_a_0_step1 = prob_not_op_0 + prob_a_0 - (prob_not_op_0 * prob_a_0);

                double prob_term5_1 = prob_and_not_op_a_1_step1 * prob_b_1;
                double prob_term5_0 = prob_and_not_op_a_0_step1 + prob_b_0 - (prob_and_not_op_a_0_step1 * prob_b_0);

                // Combinação dos Termos 4 e 5 com OR
                double prob_partial_ct2_1 = prob_term4_1 + prob_term5_1 - (prob_term4_1 * prob_term5_1);
                double prob_partial_ct2_0 = prob_term4_0 * prob_term5_0;

                // Resultado final para o carry-out
                elem.carry_out_prob_1 = prob_partial_ct1_1 + prob_partial_ct2_1 - (prob_partial_ct1_1 * prob_partial_ct2_1);
                elem.carry_out_prob_0 = prob_partial_ct1_0 * prob_partial_ct2_0;
            }
            if (elem.type == "out" && !elem.connections.empty()) {
                double source_id = elem.connections[0];
                const Element& source = netlist.at(source_id);
                    
                if(source.type == "sum_sub" && to_string(source_id).find(".2") != string::npos) {
                    elem.prob_0 = source.carry_out_prob_0;
                    elem.prob_1 = source.carry_out_prob_1;
                } else {
                    elem.prob_0 = source.prob_0;
                    elem.prob_1 = source.prob_1;
                }

            }
        }
    }
}





void calculateProbabilities(map<int, Element>& netlist) {
    // Conjunto para armazenar elementos não processados na primeira passada
    set<int> unresolved_elements;

    // Primeira passada: tenta calcular as probabilidades para todos os elementos
    for (auto& [id, elem] : netlist) {
        if (elem.connections.size() >= 1) {
            bool unresolved = false;

            // Verifica se todas as conexões já foram processadas
            for (int conn : elem.connections) {
                if (netlist.at(conn).prob_0 == -1 || netlist.at(conn).prob_1 == -1) {
                    unresolved = true;
                    break;
                }
            }

            // Se alguma conexão não foi resolvida, adicione o elemento ao conjunto de não resolvidos
            if (unresolved) {
                unresolved_elements.insert(id);
                continue;
            }

            // Calcula as probabilidades normalmente
            calculateElementProbability(netlist, elem);
        }
    }

    // Segunda passada: resolve os elementos que ficaram pendentes
    while (!unresolved_elements.empty()) {
        set<int> still_unresolved;

        for (int id : unresolved_elements) {
            Element& elem = netlist.at(id);
            bool unresolved = false;

            // Verifica novamente as conexões
            for (int conn : elem.connections) {
                if (netlist.at(conn).prob_0 == -1 || netlist.at(conn).prob_1 == -1) {
                    unresolved = true;
                    break;
                }
            }

            if (unresolved) {
                still_unresolved.insert(id);
            } else {
                calculateElementProbability(netlist, elem);
            }
        }

        // Atualiza o conjunto de elementos não resolvidos
        if (still_unresolved.size() == unresolved_elements.size()) {
            throw runtime_error("Cyclic dependency detected in netlist");
        }

        unresolved_elements = move(still_unresolved);
    }
}




// Função recursiva para rastrear todos os caminhos até uma saída
void tracePaths(int current, const map<int, Element>& netlist, vector<int>& path, vector<vector<int>>& all_paths) {
    path.push_back(current);

    const auto& elem = netlist.at(current);
    if (elem.type == "inpt") {
        // Se for uma entrada, finalizar o caminho
        all_paths.push_back(path);
    } else {
        // Continuar rastreando as conexões
        for (int conn : elem.connections) {
            tracePaths(conn, netlist, path, all_paths);
        }
    }

    path.pop_back();
}





// Função para rastrear todas as possibilidades para cada saída
void findPathsForOutputs(const map<int, Element>& netlist, map<int, vector<vector<int>>>& output_paths) {
    for (const auto& [id, elem] : netlist) {
        if (elem.type == "out") {
            vector<vector<int>> all_paths;
            vector<int> path;
            tracePaths(id, netlist, path, all_paths);
            output_paths[id] = all_paths;
        }
    }
}





// Função para exibir os caminhos das saídas
void displayOutputPaths(const map<int, vector<vector<int>>>& output_paths, const map<int, Element>& netlist, string num, string source_directory) {
    
    // Diretório onde o arquivo será salvo
    const std::string directory = "./" + source_directory + "/Outputs/";
    
    // Verifica se o diretório existe, caso contrário, cria-o
    if (!std::filesystem::exists(directory)) {
        std::filesystem::create_directory(directory);
    }

    // Caminho completo para o arquivo
    const std::string file_path = directory + "Output" + num + ".txt";

    // Abre o arquivo para escrita
    ofstream output_file(file_path);

    if (!output_file.is_open()) {
        cerr << "Error opening file for writing!" << endl;
        return;
    }

    for (const auto& [output_id, paths] : output_paths) {
        output_file << "Output " << output_id << ":\n";
        int possibility_count = 1;
        for (const auto& path : paths) {
            output_file << "  Logical Path " << possibility_count++ << ": ";
            for (size_t i = 0; i < path.size(); ++i) {
                int node_id = path[i];
                string node_representation = to_string(node_id);

                // Verifica se o node_id tem sufixo e se o elemento é do tipo 'sum_sub'
                if (i > 0) {
                    int prev_node_id = path[i - 1];
                    const auto& prev_elem = netlist.at(prev_node_id);
                    const auto& elem = netlist.at(node_id);

                    if (elem.type == "sum_sub") {
                        // Verifica se o prev_elem está fazendo a conexão com o elemento atual
                        // As conexões estão armazenadas em 'connections' no prev_elem
                        for (const auto& connection : prev_elem.connections) {
                            if (floor(connection) == node_id) {
                                // Se a conexão for com o node_id atual, pegamos o valor da conexão
                                stringstream ss;
                                ss << fixed << setprecision(1) << connection;
                                node_representation = ss.str();
                                break;
                            }
                        }
                    }
                }

                const Element& elem = netlist.at(node_id);

                // Verifica se o node_representation contém o sufixo ".2" para escolher qual probabilidade exibir
                if (node_representation.find(".2") != string::npos) {
                    output_file << node_representation << " (0: " << elem.carry_out_prob_0 << "; 1: " << elem.carry_out_prob_1 << ")";
                } else {
                    output_file << node_representation << " (0: " << elem.prob_0 << "; 1: " << elem.prob_1 << ")";
                }
                
                if (i < path.size() - 1) {
                    output_file << " <- ";
                }
            }
            output_file << "\n";
        }
        output_file << "\n"; // Linha em branco entre saídas
    }

    output_file.close(); // Fecha o arquivo
}





// Função para comparar as probabilidades e identificar divergências
vector<string> compareProbabilitiesWithPaths(
    const map<int, Element>& netlist1, const map<int, Element>& netlist2,
    const map<int, vector<vector<int>>>& output_paths1, const map<int, vector<vector<int>>>& output_paths2) {
    
    vector<string> divergences;
    const double epsilon = 1e-9;

    // Listas de saídas que ainda não encontraram um par
    // Como os 'output_paths' são maps, as listas já estarão ordenadas por ID
    list<pair<int, const vector<vector<int>>*>> unmatched1, unmatched2;
    for (const auto& [id, paths] : output_paths1) unmatched1.push_back({id, &paths});
    for (const auto& [id, paths] : output_paths2) unmatched2.push_back({id, &paths});

    // --- ETAPA ÚNICA: Comparar pares ordenados ---
    // Itera pelas duas listas, comparando o primeiro de netlist1 com o primeiro de netlist2,
    // o segundo com o segundo, e assim por diante.
    while (!unmatched1.empty() && !unmatched2.empty()) {
        const auto& out1 = unmatched1.front();
        const auto& out2 = unmatched2.front();
        const auto& elem1 = netlist1.at(out1.first);
        const auto& elem2 = netlist2.at(out2.first);

        // Verifica se as probabilidades do par atual são idênticas
        if (abs(elem1.prob_0 - elem2.prob_0) < epsilon && abs(elem1.prob_1 - elem2.prob_1) < epsilon) {
            // PAR ENCONTRADO! (Idêntico)
            // As probabilidades são iguais, então não há divergência a relatar.
        } else {
            // PAR DIVERGENTE!
            // As probabilidades são diferentes, relata a divergência.
            stringstream ss;
            ss << "Divergent Output: Output " << out1.first << " from Netlist 1 (Prob 0: " << elem1.prob_0 << ", Prob 1: " << elem1.prob_1 
               << ") diverges from Output " << out2.first << " from Netlist 2 (Prob 0: " << elem2.prob_0 << ", Prob 1: " << elem2.prob_1 << ").\n";
            divergences.push_back(ss.str());
            divergences.push_back("----------------------------------------------------------------------------------------------\n");
        }

        // Remove ambos os elementos da frente para processar o próximo par
        unmatched1.pop_front();
        unmatched2.pop_front();
    }

    // Caso 2b: A Netlist 1 tem saídas extras que não possuem par na Netlist 2.
    for (const auto& out1 : unmatched1) {
        const auto& elem1 = netlist1.at(out1.first);
        stringstream ss;
        ss << "Unmatched Output: Output " << out1.first << " from Netlist 1 (Prob 0: " << elem1.prob_0 << ", Prob 1: " << elem1.prob_1
           << ") has no equivalent in Netlist 2.\n";
        divergences.push_back(ss.str());
        divergences.push_back("----------------------------------------------------------------------------------------------\n");
    }

    // Caso 2c: A Netlist 2 tem saídas extras que não possuem par na Netlist 1.
    for (const auto& out2 : unmatched2) {
        const auto& elem2 = netlist2.at(out2.first);
        stringstream ss;
        ss << "Unmatched Output: Output " << out2.first << " from Netlist 1 (Prob 0: " << elem2.prob_0 << ", Prob 1: " << elem2.prob_1 
           << ") has no equivalent in Netlist 1.\n";
        divergences.push_back(ss.str());
        divergences.push_back("----------------------------------------------------------------------------------------------\n");
    }

    if (!divergences.empty()) {
        divergences.back().pop_back();
    }

    return divergences;
}




// Função para criar o arquivo com as divergências entre as netlists
void saveDivergences(const vector<string>& divergences, string source_directory) {
    // Diretório onde o arquivo será salvo
    const std::string directory = "./" + source_directory + "/Divergences";
    
    // Verifica se o diretório existe, caso contrário, cria-o
    if (!std::filesystem::exists(directory)) {
        std::filesystem::create_directory(directory);
    }

    // Caminho completo para o arquivo
    const std::string file_path = directory + "/Output_Divergences.txt";

    // Abre o arquivo para escrita
    ofstream file(file_path);
    
    if(divergences.size() != 0) {
        for (const auto& divergence : divergences) {
            file << divergence << "\n";
        }
    } else {
        file << "No divergences were found!";
    }

    file.close();
}





// Função para salvar as probabilidades de transição em um arquivo
void saveTransitionProbabilities(const map<int, Element>& netlist, const string& output_filename, string source_directory) {
    // Diretório onde o arquivo será salvo
    const std::string directory = "./" + source_directory + "/Table_Transitions/";
    
    // Verifica se o diretório existe, caso contrário, cria-o
    if (!std::filesystem::exists(directory)) {
        std::filesystem::create_directory(directory);
    }

    // Caminho completo para o arquivo
    const std::string file_path = directory + output_filename + ".txt";

    // Abre o arquivo para escrita
    ofstream output_file(file_path);

    if (!output_file.is_open()) {
        cerr << "Error opening file " << output_filename << " for writing!" << endl;
        return;
    }

    output_file << "Element\tTransition Probability\n";
    for (const auto& [id, elem] : netlist) {
        double transition_prob = elem.prob_0 * elem.prob_1;
        output_file << "   " << id << "\t\t      " << transition_prob << "\n";
    }


    output_file.close();
}






int main() {

    std::string filename = "./netlists/ula_limpo.txt";
    std::string filename1 = "./netlists/ula_trojan.txt";
    std::map<int, Element> netlist1, netlist2;
    std::map<int, std::vector<std::vector<int>>> output_paths1, output_paths2;
    
    // <<-- 2. Inicia o cronômetro
    auto start = std::chrono::high_resolution_clock::now();
    parseNetlist(filename, netlist1);
    parseNetlist(filename1, netlist2);

    calculateProbabilities(netlist1);
    calculateProbabilities(netlist2);

    findPathsForOutputs(netlist1, output_paths1);
    findPathsForOutputs(netlist2, output_paths2);

    auto divergences = compareProbabilitiesWithPaths(netlist1, netlist2, output_paths1, output_paths2);

    const std::string directory = "./Results";
    
    if (!std::filesystem::exists(directory)) {
        std::filesystem::create_directory(directory);
    }

    displayOutputPaths(output_paths1, netlist1, "_Netlist_Limpa", directory);
    displayOutputPaths(output_paths2, netlist2, "_Netlist_Trojan", directory);

    saveDivergences(divergences, directory);

    saveTransitionProbabilities(netlist1, "Prob_Netlist_Limpa", directory);
    saveTransitionProbabilities(netlist2, "Prob_Netlist_Trojan", directory);
 
    // <<-- 3. Para o cronômetro
    auto end = std::chrono::high_resolution_clock::now();

    // <<-- 4. Calcula a duração e converte para nanosegundos
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    // <<-- 5. Imprime o tempo de execução
    std::cout << "Tempo de execução: " << duration.count() << " us" << std::endl;

    return 0;
}