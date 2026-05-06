/*
 * ================================================================
 * ST0245 - ESTRUCTURAS DE DATOS Y ALGORITMOS
 * Universidad EAFIT - Escuela de Ciencias Aplicadas e Ingenieria
 * Docente: Alexander Narvaez Berrio
 *
 * PRACTICA II: ANALISIS EXPERIMENTAL DE ALGORITMOS
 * Comparacion entre DialSort y QuickSort (propuesta alternativa)
 *
 * Algoritmos:
 *   - DialSort-Counting : O(n + U), proyeccion por histograma
 *   - QuickSort-3Way    : mediana de tres + particion 3-way +
 *                         insercion para particiones pequenhas
 *
 * Compilar:
 *   g++ -O3 -std=c++17 -o PracticaII PracticaII.cpp
 *
 * Ejecutar:
 *   ./PracticaII                 (modo estandar)
 *   ./PracticaII --rapido        (prueba rapida, N pequenhos)
 *   ./PracticaII --grande        (hasta 10 millones de elementos)
 *   ./PracticaII --csv           (salida CSV para exportar datos)
 *   ./PracticaII --visualizar    (muestra comportamiento interno)
 *   ./PracticaII --archivo datos/dataset_predefinido.csv
 * ================================================================
 */

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <random>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace std;

// ================================================================
//  CONSTANTES GLOBALES
// ================================================================

static constexpr int      RONDAS_CALENTAMIENTO = 3;
static constexpr int      RONDAS_MEDICION      = 7;
static constexpr uint64_t SEMILLA              = 20260321ULL;
static constexpr uint64_t MAX_U_DIALSORT       = 10'000'000ULL;
static constexpr ptrdiff_t CORTE_INSERCION     = 24;

// ================================================================
//  UTILIDADES
// ================================================================

static int64_t ahora_ns()
{
    using namespace chrono;
    return duration_cast<nanoseconds>(
        high_resolution_clock::now().time_since_epoch()).count();
}

static bool esta_ordenado(const vector<int>& a)
{
    return is_sorted(a.begin(), a.end());
}

static const char* nombre_compilador()
{
#if defined(__clang__)
    return "clang++";
#elif defined(__GNUC__)
    return "g++";
#elif defined(_MSC_VER)
    return "MSVC";
#else
    return "desconocido";
#endif
}

static string valor_arg(int argc, char** argv, const string& arg)
{
    for (int i = 1; i + 1 < argc; ++i)
        if (argv[i] == arg) return argv[i + 1];
    return "";
}

static bool cargar_dataset_archivo(const string& ruta, vector<int>& datos)
{
    ifstream archivo(ruta);
    if (!archivo) {
        cerr << "[ERROR] No se pudo abrir el dataset: " << ruta << "\n";
        return false;
    }

    datos.clear();
    string linea;
    size_t linea_actual = 0;

    while (getline(archivo, linea)) {
        ++linea_actual;
        for (char& ch : linea) {
            if (ch == ',' || ch == ';' || ch == '\t')
                ch = ' ';
        }

        istringstream iss(linea);
        long long valor = 0;
        while (iss >> valor) {
            if (valor < numeric_limits<int>::min() ||
                valor > numeric_limits<int>::max()) {
                cerr << "[ERROR] Valor fuera del rango int en linea "
                     << linea_actual << ": " << valor << "\n";
                return false;
            }
            datos.push_back(static_cast<int>(valor));
        }

        if (!iss.eof()) {
            cerr << "[ERROR] Token invalido en el dataset, linea "
                 << linea_actual << "\n";
            return false;
        }
    }

    if (datos.empty()) {
        cerr << "[ERROR] El dataset esta vacio: " << ruta << "\n";
        return false;
    }

    return true;
}

// Estadisticas descriptivas sobre un vector de tiempos en ns
struct Estadisticas {
    double mejor_ms  = 0.0;
    double media_ms  = 0.0;
    double desv_ms   = 0.0;
    double throughput = 0.0;   // millones de claves / segundo (mejor tiempo)
};

static Estadisticas calcular_estadisticas(const vector<int64_t>& tiempos_ns,
                                          size_t n)
{
    Estadisticas e;
    int64_t mejor = *min_element(tiempos_ns.begin(), tiempos_ns.end());
    e.mejor_ms = static_cast<double>(mejor) / 1e6;

    double suma = 0.0;
    for (int64_t t : tiempos_ns) suma += static_cast<double>(t) / 1e6;
    e.media_ms = suma / static_cast<double>(tiempos_ns.size());

    double var = 0.0;
    for (int64_t t : tiempos_ns) {
        double d = static_cast<double>(t) / 1e6 - e.media_ms;
        var += d * d;
    }
    e.desv_ms = sqrt(var / static_cast<double>(tiempos_ns.size()));

    // Throughput basado en el mejor tiempo
    e.throughput = (static_cast<double>(n) /
                    (static_cast<double>(mejor) / 1e9)) / 1e6;
    return e;
}

// Memoria estimada en KB
static pair<bool, uint64_t> tamano_universo(int mn, int mx)
{
    const uint64_t U = static_cast<uint64_t>(
        static_cast<int64_t>(mx) - static_cast<int64_t>(mn)) + 1ULL;
    return {U <= MAX_U_DIALSORT, U};
}

static size_t memoria_dialsort_kb(const vector<int>& a)
{
    if (a.empty()) return 0;

    const auto [mn_it, mx_it] = minmax_element(a.begin(), a.end());
    const auto [ok, U64] = tamano_universo(*mn_it, *mx_it);
    if (!ok) return (a.size() * sizeof(int)) / 1024;

    // vector de entrada (n ints) + histograma H (U ints), como DialSort original
    return (a.size() * sizeof(int) + static_cast<size_t>(U64) * sizeof(int)) / 1024;
}

static size_t memoria_quicksort_kb(size_t n)
{
    // vector de entrada (n ints) + pila recursiva estimada O(log n) * frame
    return (n * sizeof(int) + static_cast<size_t>(log2(n + 1)) * 64) / 1024;
}

// ================================================================
//  DIALSORT - Ordenamiento por histograma (Counting Sort / Dial)
// ================================================================

/*
 * Funcionamiento:
 *   1. Encuentra el rango real [mn, mx] del arreglo.
 *   2. Construye un histograma H[k - mn] contando cuantas veces aparece
 *      cada valor.  Costo: O(n).
 *   3. Recorre el histograma de menor a mayor y re-escribe el arreglo
 *      original con las repeticiones correspondientes.  Costo: O(U).
 *   Costo total: O(n + U)
 *
 * Ventaja: lineal cuando U es pequenho comparado con n.
 * Desventaja: requiere O(U) memoria extra; ineficiente si U >> n.
 */
static bool dialsort(vector<int>& arreglo)
{
    const size_t n = arreglo.size();
    if (n <= 1) return true;

    int mn = arreglo[0], mx = arreglo[0];
    for (size_t i = 1; i < n; ++i) {
        if (arreglo[i] < mn) mn = arreglo[i];
        if (arreglo[i] > mx) mx = arreglo[i];
    }

    auto [ok, U64] = tamano_universo(mn, mx);
    if (!ok) {
        cerr << "[WARN] dialsort: U > MAX_U_DIALSORT. Omitido.\n";
        return false;
    }
    const size_t U = static_cast<size_t>(U64);

    // Paso 1: construir histograma H[k - mn]
    vector<int> H(U, 0);
    for (size_t i = 0; i < n; ++i)
        H[static_cast<size_t>(arreglo[i] - mn)]++;

    // Paso 2: reconstruir arreglo ordenado
    size_t salida = 0;
    for (size_t y = 0; y < U; ++y) {
        const int valor = static_cast<int>(y) + mn;
        for (int c = H[y]; c > 0; --c)
            arreglo[salida++] = valor;
    }
    return true;
}

// ================================================================
//  QUICKSORT - Mediana de tres + particion 3-way
// ================================================================

/*
 * Funcionamiento:
 *   1. Ordenamiento por insercion para particiones <= CORTE_INSERCION.
 *      Muy eficiente en cache para arreglos pequenhos.
 *   2. Pivote elegido como mediana de tres (primero, medio, ultimo).
 *      Evita el peor caso O(n^2) en datos ya ordenados.
 *   3. Particion 3-way (Bentley-McIlroy): divide en < pivote,
 *      == pivote y > pivote. Eficiente con claves repetidas.
 *   4. Recurre solo en el lado mas pequenho (tail-call en el grande).
 *      Garantiza pila O(log n).
 *   Costo promedio: O(n log n) | Peor caso: O(n^2) (muy raro)
 */
static void ordenamiento_insercion(vector<int>& arreglo,
                                   ptrdiff_t inicio,
                                   ptrdiff_t fin)
{
    for (ptrdiff_t i = inicio + 1; i <= fin; ++i) {
        const int clave = arreglo[static_cast<size_t>(i)];
        ptrdiff_t j = i - 1;
        while (j >= inicio && arreglo[static_cast<size_t>(j)] > clave) {
            arreglo[static_cast<size_t>(j + 1)] = arreglo[static_cast<size_t>(j)];
            --j;
        }
        arreglo[static_cast<size_t>(j + 1)] = clave;
    }
}

static int mediana_de_tres(int x, int y, int z)
{
    if (x < y) {
        if (y < z) return y;
        return x < z ? z : x;
    }
    if (x < z) return x;
    return y < z ? z : y;
}

static void quicksort_3way(vector<int>& arreglo,
                           ptrdiff_t inicio,
                           ptrdiff_t fin)
{
    while (fin - inicio > CORTE_INSERCION) {
        const ptrdiff_t medio = inicio + (fin - inicio) / 2;
        const int pivote = mediana_de_tres(
            arreglo[static_cast<size_t>(inicio)],
            arreglo[static_cast<size_t>(medio)],
            arreglo[static_cast<size_t>(fin)]);

        ptrdiff_t menor  = inicio;
        ptrdiff_t igual  = inicio;
        ptrdiff_t mayor  = fin;

        while (igual <= mayor) {
            const int valor = arreglo[static_cast<size_t>(igual)];
            if (valor < pivote) {
                swap(arreglo[static_cast<size_t>(menor)],
                     arreglo[static_cast<size_t>(igual)]);
                ++menor; ++igual;
            } else if (pivote < valor) {
                swap(arreglo[static_cast<size_t>(igual)],
                     arreglo[static_cast<size_t>(mayor)]);
                --mayor;
            } else {
                ++igual;
            }
        }

        // Recursion en el lado mas pequenho, iteracion en el grande
        const ptrdiff_t tam_izq = menor - inicio;
        const ptrdiff_t tam_der = fin - mayor;
        if (tam_izq < tam_der) {
            if (inicio < menor - 1) quicksort_3way(arreglo, inicio, menor - 1);
            inicio = mayor + 1;
        } else {
            if (mayor + 1 < fin) quicksort_3way(arreglo, mayor + 1, fin);
            fin = menor - 1;
        }
    }
    if (inicio < fin) ordenamiento_insercion(arreglo, inicio, fin);
}

static bool quicksort(vector<int>& arreglo, int /*U*/)
{
    if (arreglo.size() <= 1) return true;
    quicksort_3way(arreglo, 0, static_cast<ptrdiff_t>(arreglo.size() - 1));
    return true;
}

// ================================================================
//  GENERADORES DE DATOS
// ================================================================

/*
 * Uniforme: cada clave tiene igual probabilidad de aparecer.
 *           Caso promedio tipico para ambos algoritmos.
 */
static vector<int> gen_uniforme(size_t n, int U, uint64_t semilla)
{
    mt19937_64 rng(semilla);
    uniform_int_distribution<int> dist(0, U - 1);
    vector<int> a(n);
    for (int& x : a) x = dist(rng);
    return a;
}

/*
 * Sesgada: 80% de los valores caen en el 5% inferior del universo.
 *          Favorece a DialSort (histograma denso al inicio) y
 *          a QuickSort-3way (muchos iguales => particion eficiente).
 */
static vector<int> gen_sesgada(size_t n, int U, uint64_t semilla)
{
    mt19937_64 rng(semilla);
    const int limite_caliente = max(1, U / 20);
    uniform_int_distribution<int> caliente(0, limite_caliente - 1);
    uniform_int_distribution<int> fria(0, U - 1);
    bernoulli_distribution elegir_caliente(0.80);

    vector<int> a(n);
    for (int& x : a) x = elegir_caliente(rng) ? caliente(rng) : fria(rng);
    return a;
}

/*
 * Ordenada: mejor caso para DialSort (histograma ya denso).
 *           Peor caso potencial para QuickSort simple, pero la
 *           mediana-de-tres lo mitiga.
 */
static vector<int> gen_ordenada(size_t n, int U, uint64_t semilla)
{
    auto a = gen_uniforme(n, U, semilla);
    sort(a.begin(), a.end());
    return a;
}

/*
 * Inversa: ordenada de mayor a menor.
 *          Estresa a QuickSort sin mediana-de-tres.
 */
static vector<int> gen_inversa(size_t n, int U, uint64_t semilla)
{
    auto a = gen_ordenada(n, U, semilla);
    reverse(a.begin(), a.end());
    return a;
}

// ================================================================
//  ESTRUCTURAS DE RESULTADO
// ================================================================

struct FilaResultado {
    string   algoritmo;
    string   distribucion;
    size_t   n           = 0;
    int      U           = 0;
    bool     correcto    = false;
    bool     omitido     = false;
    Estadisticas stats;
    size_t   memoria_kb  = 0;
    double   ratio_dial_quick = 0.0;
};

struct ResultadoPar {
    FilaResultado dialsort;
    FilaResultado quicksort;
    double ratio = 0.0;
};

// ================================================================
//  BENCHMARK: ejecutar un algoritmo y medir
// ================================================================

using FnOrdenamiento = function<bool(vector<int>&, int)>;

static FilaResultado ejecutar_uno(const string&        algoritmo,
                                  const string&        distribucion,
                                  const vector<int>&   base,
                                  int                  U,
                                  const FnOrdenamiento& fn,
                                  size_t               mem_kb)
{
    FilaResultado fila;
    fila.algoritmo     = algoritmo;
    fila.distribucion  = distribucion;
    fila.n             = base.size();
    fila.U             = U;
    fila.memoria_kb    = mem_kb;

    // Calentamiento (descartado)
    for (int r = 0; r < RONDAS_CALENTAMIENTO; ++r) {
        auto tmp = base;
        if (!fn(tmp, U)) { fila.omitido = true; return fila; }
        if (!esta_ordenado(tmp)) { fila.correcto = false; return fila; }
    }

    // Medicion real
    vector<int64_t> tiempos;
    tiempos.reserve(RONDAS_MEDICION);

    for (int r = 0; r < RONDAS_MEDICION; ++r) {
        auto tmp = base;
        const int64_t inicio = ahora_ns();
        if (!fn(tmp, U)) { fila.omitido = true; return fila; }
        const int64_t transcurrido = ahora_ns() - inicio;
        if (!esta_ordenado(tmp)) { fila.correcto = false; return fila; }
        tiempos.push_back(transcurrido);
    }

    fila.correcto = true;
    fila.stats    = calcular_estadisticas(tiempos, base.size());
    return fila;
}

// ================================================================
//  VISUALIZACION DEL COMPORTAMIENTO INTERNO
// ================================================================

static void visualizar_dialsort(size_t n_vis = 30, int U_vis = 10)
{
    cout << "\n";
    cout << "================================================================\n";
    cout << "  VISUALIZACION: DIALSORT (n=" << n_vis
         << ", U_generado=" << U_vis << ")\n";
    cout << "================================================================\n";

    mt19937_64 rng(42);
    uniform_int_distribution<int> dist(0, U_vis - 1);
    vector<int> arreglo(n_vis);
    for (int& x : arreglo) x = dist(rng);

    cout << "\n  Arreglo original:\n  [ ";
    for (int x : arreglo) cout << x << " ";
    cout << "]\n";

    const auto [mn_it, mx_it] = minmax_element(arreglo.begin(), arreglo.end());
    const int mn = *mn_it;
    const int mx = *mx_it;
    const auto [ok, U64] = tamano_universo(mn, mx);
    if (!ok) {
        cout << "\n  Rango demasiado grande para visualizar DialSort.\n";
        return;
    }
    const size_t U = static_cast<size_t>(U64);

    // Paso 1: construir histograma H[k - mn]
    vector<int> H(U, 0);
    for (int x : arreglo) H[static_cast<size_t>(x - mn)]++;

    cout << "\n  Rango real detectado: [" << mn << ", " << mx << "]\n";
    cout << "\n  Histograma (valor -> frecuencia -> barra):\n";
    for (size_t y = 0; y < U; ++y) {
        const int valor = static_cast<int>(y) + mn;
        cout << "  Valor " << setw(2) << valor << " | "
             << setw(3) << H[y] << " | ";
        for (int k = 0; k < H[y]; ++k)
            cout << "##";
        cout << "\n";
    }

    // Paso 2: reconstruir
    size_t salida = 0;
    for (size_t y = 0; y < U; ++y) {
        const int valor = static_cast<int>(y) + mn;
        for (int c = H[y]; c > 0; --c)
            arreglo[salida++] = valor;
    }

    cout << "\n  Arreglo ordenado:\n  [ ";
    for (int x : arreglo) cout << x << " ";
    cout << "]\n";
    cout << "\n  -> DialSort NO realiza comparaciones entre elementos.\n";
    cout << "     Ordena leyendo el histograma de menor a mayor.\n";
}

static void visualizar_quicksort(size_t n_vis = 20, int U_vis = 50)
{
    cout << "\n";
    cout << "================================================================\n";
    cout << "  VISUALIZACION: QUICKSORT-3WAY (n=" << n_vis << ", U=" << U_vis << ")\n";
    cout << "================================================================\n";

    mt19937_64 rng(42);
    uniform_int_distribution<int> dist(0, U_vis - 1);
    vector<int> arreglo(n_vis);
    for (int& x : arreglo) x = dist(rng);

    cout << "\n  Arreglo original:\n  [ ";
    for (int x : arreglo) cout << setw(3) << x;
    cout << " ]\n";

    // Mostrar el pivote y las 3 zonas de la particion inicial
    ptrdiff_t inicio = 0;
    ptrdiff_t fin    = static_cast<ptrdiff_t>(n_vis) - 1;
    ptrdiff_t medio  = inicio + (fin - inicio) / 2;
    int pivote = mediana_de_tres(arreglo[0],
                                 arreglo[static_cast<size_t>(medio)],
                                 arreglo[static_cast<size_t>(fin)]);

    cout << "\n  Pivote (mediana de tres): " << pivote << "\n";
    cout << "  Candidatos: primero=" << arreglo[0]
         << "  medio=" << arreglo[static_cast<size_t>(medio)]
         << "  ultimo=" << arreglo[static_cast<size_t>(fin)] << "\n";

    // Aplicar particion 3-way y mostrar resultado
    vector<int> menores, iguales, mayores;
    for (int x : arreglo) {
        if (x < pivote)      menores.push_back(x);
        else if (x == pivote) iguales.push_back(x);
        else                  mayores.push_back(x);
    }

    cout << "\n  Despues de particionar:\n";
    cout << "  [ MENORES: ";
    for (int x : menores) cout << x << " ";
    cout << "] [ IGUALES: ";
    for (int x : iguales) cout << x << " ";
    cout << "] [ MAYORES: ";
    for (int x : mayores) cout << x << " ";
    cout << "]\n";

    cout << "\n  La zona IGUALES no se vuelve a procesar (optimizacion 3-way).\n";
    cout << "  Recursion solo en MENORES (" << menores.size()
         << " elem) y MAYORES (" << mayores.size() << " elem).\n";

    // Resultado final
    quicksort(arreglo, 0);
    cout << "\n  Arreglo ordenado:\n  [ ";
    for (int x : arreglo) cout << setw(3) << x;
    cout << " ]\n";
}

// ================================================================
//  IMPRESION DE TABLAS
// ================================================================

static void separador(int ancho = 130)
{
    cout << string(static_cast<size_t>(ancho), '-') << "\n";
}

static void imprimir_encabezado()
{
    cout << left
         << setw(22) << "Algoritmo"
         << setw(11) << "Dist."
         << setw(12) << "N"
         << setw(8)  << "U"
         << setw(12) << "Mejor(ms)"
         << setw(12) << "Media(ms)"
         << setw(12) << "DesvEst(ms)"
         << setw(13) << "M claves/s"
         << setw(12) << "Mem(KB)"
         << setw(13) << "Dial/Quick"
         << "OK\n";
    separador();
}

static void imprimir_fila(const FilaResultado& f)
{
    if (f.omitido) {
        cout << left
             << setw(22) << f.algoritmo
             << setw(11) << f.distribucion
             << setw(12) << f.n
             << setw(8)  << f.U
             << "[OMITIDO - U demasiado grande para DialSort]\n";
        return;
    }

    cout << left
         << setw(22) << f.algoritmo
         << setw(11) << f.distribucion
         << setw(12) << f.n
         << setw(8)  << f.U
         << fixed << setprecision(3)
         << setw(12) << f.stats.mejor_ms
         << setw(12) << f.stats.media_ms
         << setw(12) << f.stats.desv_ms
         << setw(13) << f.stats.throughput
         << setw(12) << f.memoria_kb;

    if (f.ratio_dial_quick > 0.0)
        cout << setw(13) << f.ratio_dial_quick;
    else
        cout << setw(13) << "-";

    cout << (f.correcto ? "OK" : "FALLO") << "\n";
}

static void fila_csv(const FilaResultado& f)
{
    if (f.omitido) {
        cout << f.algoritmo << "," << f.distribucion << ","
             << f.n << "," << f.U
             << ",OMITIDO,OMITIDO,OMITIDO,OMITIDO,OMITIDO,OMITIDO\n";
        return;
    }
    cout << f.algoritmo << ","
         << f.distribucion << ","
         << f.n << ","
         << f.U << ","
         << fixed << setprecision(3)
         << f.stats.mejor_ms << ","
         << f.stats.media_ms << ","
         << f.stats.desv_ms  << ","
         << f.stats.throughput << ","
         << f.memoria_kb << ","
         << f.ratio_dial_quick << ","
         << (f.correcto ? "OK" : "FALLO") << "\n";
}

// ================================================================
//  ANALISIS DE COMPLEJIDAD
// ================================================================

static void imprimir_analisis_complejidad()
{
    cout << "\n================================================================\n";
    cout << "  ANALISIS DE COMPLEJIDAD\n";
    cout << "================================================================\n";

    cout << "\n[DialSort-Counting]\n"
            "  Mejor caso    : O(n + U)  -- siempre (no depende del orden)\n"
            "  Caso promedio : O(n + U)\n"
            "  Peor caso     : O(n + U)  -- lineal garantizado\n"
            "  Espacio       : O(U) extra para el histograma H[k - mn]\n"
            "  Estabilidad   : Estable (preserva orden de elementos iguales\n"
            "                  si se usa version con indices de salida)\n"
            "  Restriccion   : Solo funciona con claves enteras acotadas.\n"
            "                  Si U >> n el histograma desperdicia memoria.\n"
            "  Ideal cuando  : U es pequenho y n es grande (muchas repeticiones).\n";

    cout << "\n[QuickSort-3Way (Mediana de Tres)]\n"
            "  Mejor caso    : O(n log n)\n"
            "  Caso promedio : O(n log n)\n"
            "  Peor caso     : O(n^2)  -- muy raro con mediana de tres\n"
            "  Espacio       : O(log n) en pila de llamadas (tail recursion)\n"
            "  Estabilidad   : No estable\n"
            "  Ventaja clave : La particion 3-way hace que datos con muchas\n"
            "                  claves repetidas sean O(n) en la practica.\n"
            "  Ventaja clave : Opera sobre claves de cualquier tipo comparable.\n"
            "  Ideal cuando  : Datos generales, claves no acotadas, o U >> n.\n";

    cout << "\n[Comparacion teorica]\n"
            "  Cuando U = O(n)  : DialSort gana (O(n) vs O(n log n))\n"
            "  Cuando U = O(n^2): Empatan asintoticamente\n"
            "  Cuando U >> n    : QuickSort gana en memoria; DialSort puede\n"
            "                     quedar inviable por el histograma gigante.\n"
            "  En la practica   : Factores de cache y branch prediction hacen\n"
            "                     que QuickSort sea competitivo incluso con\n"
            "                     U pequenho, especialmente para n moderado.\n";
}

// ================================================================
//  ARGUMENTOS DE LINEA DE COMANDOS
// ================================================================

static bool tiene_arg(int argc, char** argv, const string& arg)
{
    for (int i = 1; i < argc; ++i)
        if (argv[i] == arg) return true;
    return false;
}

// ================================================================
//  MAIN
// ================================================================

int main(int argc, char** argv)
{
    const bool modo_rapido    = tiene_arg(argc, argv, "--rapido");
    const bool modo_grande    = tiene_arg(argc, argv, "--grande");
    const bool modo_csv       = tiene_arg(argc, argv, "--csv");
    const bool modo_visualizar = tiene_arg(argc, argv, "--visualizar");
    string ruta_dataset = valor_arg(argc, argv, "--archivo");
    if (ruta_dataset.empty())
        ruta_dataset = valor_arg(argc, argv, "--dataset");

    // --- Visualizacion del comportamiento interno ---
    if (modo_visualizar) {
        visualizar_dialsort();
        visualizar_quicksort();
        cout << "\n";
    }

    // --- Configuracion de tamanhos y universos ---
    const vector<size_t> Ns = modo_rapido
        ? vector<size_t>{1'000, 10'000, 100'000}
        : (modo_grande
           ? vector<size_t>{100'000, 500'000, 1'000'000,
                            5'000'000, 10'000'000}
           : vector<size_t>{100'000, 500'000, 1'000'000});

    const vector<int> Us = modo_rapido
        ? vector<int>{256, 1024}
        : vector<int>{256, 1024, 65'536};

    // --- Distribuciones ---
    using FnGen = vector<int>(*)(size_t, int, uint64_t);
    struct Distribucion { string nombre; FnGen gen; };

    const vector<Distribucion> distribuciones = modo_rapido
        ? vector<Distribucion>{{"uniforme", gen_uniforme},
                               {"sesgada",  gen_sesgada}}
        : vector<Distribucion>{{"uniforme", gen_uniforme},
                               {"sesgada",  gen_sesgada},
                               {"ordenada", gen_ordenada},
                               {"inversa",  gen_inversa}};

    const FnOrdenamiento fn_dial  = [](vector<int>& a, int) {
        return dialsort(a);
    };
    const FnOrdenamiento fn_quick = [](vector<int>& a, int U) {
        return quicksort(a, U);
    };

    if (!ruta_dataset.empty()) {
        vector<int> base;
        if (!cargar_dataset_archivo(ruta_dataset, base))
            return EXIT_FAILURE;

        const auto [mn_it, mx_it] = minmax_element(base.begin(), base.end());
        const auto [ok_u, U64] = tamano_universo(*mn_it, *mx_it);
        const int U_dataset = ok_u ? static_cast<int>(U64) : -1;

        cout << "================================================================\n"
             << "  ST0245 - PRACTICA II: ANALISIS EXPERIMENTAL\n"
             << "  DialSort-Counting vs. QuickSort-3Way\n"
             << "================================================================\n"
             << "  Compilador      : " << nombre_compilador() << "\n"
             << "  Estandar        : C++17\n"
             << "  Calentamiento   : " << RONDAS_CALENTAMIENTO << " rondas descartadas\n"
             << "  Medicion        : mejor de " << RONDAS_MEDICION << " rondas\n"
             << "  Modo            : dataset predefinido\n"
             << "  Archivo         : " << ruta_dataset << "\n"
             << "  N               : " << base.size() << "\n"
             << "  Rango real      : [" << *mn_it << ", " << *mx_it << "]\n"
             << "  U detectado     : " << (ok_u ? to_string(U64) : string("fuera de dominio")) << "\n"
             << "================================================================\n\n";

        imprimir_encabezado();

        auto fila_quick = ejecutar_uno(
            "QuickSort-3Way", "archivo", base, U_dataset,
            fn_quick, memoria_quicksort_kb(base.size()));

        auto fila_dial = ejecutar_uno(
            "DialSort-Counting", "archivo", base, U_dataset,
            fn_dial, memoria_dialsort_kb(base));

        double ratio = 0.0;
        if (!fila_dial.omitido && !fila_quick.omitido &&
            fila_quick.stats.mejor_ms > 0.0) {
            ratio = fila_dial.stats.mejor_ms / fila_quick.stats.mejor_ms;
            fila_dial.ratio_dial_quick = ratio;
        }

        imprimir_fila(fila_dial);
        imprimir_fila(fila_quick);

        cout << "\n================================================================\n"
             << "  RESUMEN DATASET PREDEFINIDO\n"
             << "================================================================\n"
             << fixed << setprecision(3)
             << "  Configuraciones medidas            : "
             << ((!fila_dial.omitido && !fila_quick.omitido) ? 1 : 0) << "\n"
             << "  Ratio Dial/Quick                    : "
             << (ratio > 0.0 ? ratio : 0.0) << "x\n"
             << "  Verificacion de correctitud         : "
             << ((fila_dial.correcto && fila_quick.correcto) ? "TODOS CORRECTOS" : "HAY FALLOS")
             << "\n\n";

        imprimir_analisis_complejidad();

        if (modo_csv) {
            cout << "\n================================================================\n"
                 << "  SALIDA CSV\n"
                 << "================================================================\n"
                 << "algoritmo,distribucion,N,U,mejor_ms,media_ms,desv_ms,"
                    "M_claves_s,memoria_kb,ratio_dial_quick,correcto\n";
            fila_csv(fila_dial);
            fila_csv(fila_quick);
        }

        return (fila_dial.correcto && fila_quick.correcto)
            ? EXIT_SUCCESS
            : EXIT_FAILURE;
    }

    // --- Encabezado general ---
    cout << "================================================================\n"
         << "  ST0245 - PRACTICA II: ANALISIS EXPERIMENTAL\n"
         << "  DialSort-Counting vs. QuickSort-3Way\n"
         << "================================================================\n"
         << "  Compilador      : " << nombre_compilador() << "\n"
         << "  Estandar        : C++17\n"
         << "  Calentamiento   : " << RONDAS_CALENTAMIENTO << " rondas descartadas\n"
         << "  Medicion        : mejor de " << RONDAS_MEDICION << " rondas\n"
         << "  Semilla         : " << SEMILLA << "\n"
         << "  Modo            : "
         << (modo_rapido ? "rapido" : (modo_grande ? "grande" : "estandar")) << "\n"
         << "  DialSort        : O(n + U), rango real [mn,mx] + histograma\n"
         << "  QuickSort       : mediana-de-tres, 3-way, corte insercion "
         << CORTE_INSERCION << "\n"
         << "  Columna Dial/Q  : ms(DialSort) / ms(QuickSort) "
            "-- menor que 1 => DialSort mas rapido\n"
         << "================================================================\n\n";

    // --- Benchmark principal ---
    vector<ResultadoPar> resultados;
    imprimir_encabezado();

    for (size_t n : Ns) {
        for (int U : Us) {
            for (const auto& dist : distribuciones) {

                const uint64_t semilla_caso =
                    SEMILLA
                    ^ (static_cast<uint64_t>(n)             * 1'000'003ULL)
                    ^ (static_cast<uint64_t>(U)              * 7'919ULL)
                    ^ (static_cast<uint64_t>(dist.nombre.size()) * 101ULL);

                const auto base = dist.gen(n, U, semilla_caso);

                // QuickSort primero (referencia)
                auto fila_quick = ejecutar_uno(
                    "QuickSort-3Way", dist.nombre, base, U,
                    fn_quick, memoria_quicksort_kb(n));
                fila_quick.stats.throughput =
                    fila_quick.omitido ? 0.0 : fila_quick.stats.throughput;

                // DialSort
                auto fila_dial = ejecutar_uno(
                    "DialSort-Counting", dist.nombre, base, U,
                    fn_dial, memoria_dialsort_kb(base));

                // Calcular ratio
                double ratio = 0.0;
                if (!fila_dial.omitido && !fila_quick.omitido
                    && fila_quick.stats.mejor_ms > 0.0) {
                    ratio = fila_dial.stats.mejor_ms / fila_quick.stats.mejor_ms;
                    fila_dial.ratio_dial_quick = ratio;
                }

                imprimir_fila(fila_dial);
                imprimir_fila(fila_quick);
                cout << "\n";

                resultados.push_back({fila_dial, fila_quick, ratio});
            }
        }
    }

    // --- Resumen estadistico ---
    int medidos     = 0;
    int gana_dial   = 0;
    int gana_quick  = 0;
    int empate      = 0;
    double suma_ratio       = 0.0;
    double mejor_speedup    = 0.0;
    double peor_ratio       = 0.0;
    bool todo_correcto      = true;

    for (const auto& r : resultados) {
        if ((!r.dialsort.omitido  && !r.dialsort.correcto) ||
            (!r.quicksort.omitido && !r.quicksort.correcto))
            todo_correcto = false;

        if (r.dialsort.omitido || r.quicksort.omitido || r.ratio <= 0.0)
            continue;

        ++medidos;
        suma_ratio   += r.ratio;
        mejor_speedup = max(mejor_speedup, r.dialsort.stats.throughput /
                                           max(1e-9, r.quicksort.stats.throughput));
        peor_ratio    = max(peor_ratio, r.ratio);

        if      (r.ratio < 0.99) ++gana_dial;
        else if (r.ratio > 1.01) ++gana_quick;
        else                     ++empate;
    }

    cout << "================================================================\n"
         << "  RESUMEN\n"
         << "================================================================\n"
         << fixed << setprecision(3)
         << "  Configuraciones medidas            : " << medidos << "\n"
         << "  DialSort mas rapido  (ratio < 0.99): "
         << gana_dial  << " / " << medidos << "\n"
         << "  QuickSort mas rapido (ratio > 1.01): "
         << gana_quick << " / " << medidos << "\n"
         << "  Empate efectivo                    : "
         << empate     << " / " << medidos << "\n"
         << "  Ratio promedio Dial/Quick           : "
         << (medidos > 0 ? suma_ratio / medidos : 0.0) << "x\n"
         << "  Mejor ventaja de DialSort           : "
         << mejor_speedup << "x throughput\n"
         << "  Peor ratio Dial/Quick               : " << peor_ratio << "x\n"
         << "  Verificacion de correctitud         : "
         << (todo_correcto ? "TODOS CORRECTOS" : "HAY FALLOS") << "\n\n";

    // --- Analisis de complejidad ---
    imprimir_analisis_complejidad();

    // --- Conclusiones ---
    cout << "\n================================================================\n"
         << "  CONCLUSIONES\n"
         << "================================================================\n"
         << "  1. DialSort domina cuando U es pequenho (256, 1024):\n"
         << "     el histograma es compacto y el recorrido lineal O(n + U)\n"
         << "     supera claramente a O(n log n) de QuickSort.\n\n"
         << "  2. Con U = 65.536 la ventaja de DialSort se reduce:\n"
         << "     el histograma ocupa mas memoria y hay mas fallos de cache\n"
         << "     al recorrerlo, acercandose al costo de QuickSort.\n\n"
         << "  3. QuickSort-3Way es robusto en todas las distribuciones:\n"
         << "     la mediana de tres evita el peor caso en datos ordenados,\n"
         << "     y la particion 3-way es eficiente con claves repetidas.\n\n"
         << "  4. Big-O no lo es todo: la localidad de cache y la prediccion\n"
         << "     de ramas explican por que QuickSort compite incluso con\n"
         << "     universos donde DialSort deberia ser asintoticamente mejor.\n\n"
         << "  5. Eleccion practica:\n"
         << "     - Usar DialSort cuando U <= n y las claves son enteros\n"
         << "       acotados (clasificacion de notas, edades, codigos).\n"
         << "     - Usar QuickSort para datos generales, claves no acotadas\n"
         << "       o cuando la memoria extra de O(U) es un problema.\n"
         << "================================================================\n\n";

    // --- Salida CSV (opcional) ---
    if (modo_csv) {
        cout << "================================================================\n"
             << "  SALIDA CSV\n"
             << "================================================================\n"
             << "algoritmo,distribucion,N,U,mejor_ms,media_ms,desv_ms,"
                "M_claves_s,memoria_kb,ratio_dial_quick,correcto\n";
        for (const auto& r : resultados) {
            fila_csv(r.dialsort);
            fila_csv(r.quicksort);
        }
    }

    return todo_correcto ? EXIT_SUCCESS : EXIT_FAILURE;
}
