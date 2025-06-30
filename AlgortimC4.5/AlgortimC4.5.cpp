#include <windows.h>      // Основные функции Windows API
#include <commdlg.h>      // Диалоги открытия/сохранения файлов
#include <commctrl.h>     // Общие элементы управления Windows
#include <fstream>        // Работа с файлами
#include <sstream>        // Строковые потоки для парсинга
#include <vector>         // Динамические массивы
#include <string>         // Работа со строками
#include <algorithm>      // Алгоритмы сортировки и поиска
#include <map>            // Ассоциативные контейнеры для подсчета
#include <cmath>          // Математические функции (log2)
#include <iomanip>        // Форматирование вывода
#include <locale>         // Локализация
#include <codecvt>        // Конвертация кодировок

#pragma comment(lib, "comctl32.lib")  // Подключение библиотеки элементов управления

//КОНСТАНТЫ ИДЕНТИФИКАТОРОВ ЭЛЕМЕНТОВ ИНТЕРФЕЙСА
#define ID_LOAD_BUTTON 1001      // Кнопка загрузки CSV файла
#define ID_CALCULATE_BUTTON 1002 // Кнопка построения дерева решений
#define ID_SAVE_BUTTON 1003      // Кнопка сохранения результатов
#define ID_LISTBOX 1004          // Список столбцов CSV файла
#define ID_RESULTS_TEXT 1005     // Текстовое поле для вывода результатов

//ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ ИНТЕРФЕЙСА
HWND hMainWindow;                    // Вызов главного окна приложения
HWND hLoadButton, hCalculateButton, hSaveButton;  // Вызовы кнопок
HWND hListBox, hResultsText;         // Вызов списка и текстового поля

//ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ ДАННЫХ
std::vector<std::vector<std::string>> csvData;  // Двумерный массив данных из CSV файла
std::vector<std::string> columnNames;           // Названия столбцов (заголовки CSV)
std::wstring resultsText;                       // Текст результатов анализа для отображения
char detectedDelimiter = ',';                   // Обнаруженный разделитель

//СТРУКТУРА УЗЛА ДЕРЕВА РЕШЕНИЙ
struct DecisionNode {
    bool isLeaf;
    int attributeIndex;
    std::string attributeName;
    double threshold;

    //МЕТРИКИ КАЧЕСТВА C4.5
    double informationGain;
    double splitInformation;
    double gainRatio;
    double entropy;

    //ДАННЫЕ УЗЛА
    std::vector<int> yValues;
    int predictedClass;

    //СТРУКТУРА ДЕРЕВА
    std::unique_ptr<DecisionNode> leftChild;
    std::unique_ptr<DecisionNode> rightChild;
    int depth;

    //ОПИСАНИЕ ДЛЯ ВЫВОДА
    std::wstring nodeDescription;

    DecisionNode() : isLeaf(false), attributeIndex(-1), threshold(0.0),
        informationGain(0.0), splitInformation(0.0), gainRatio(0.0), entropy(0.0),
        predictedClass(-1), depth(0) {
    }
};

//СТРУКТУРА ПОДМНОЖЕСТВА ДАННЫХ
struct DataSubset {
    std::vector<std::vector<double>> attributeValues;
    std::vector<int> yValues;
    std::vector<int> originalRowIndices;
};

// ФУНКЦИИ КОНВЕРТАЦИИ КОДИРОВОК
std::wstring utf8_to_wstring(const std::string& str) {
    if (str.empty()) return std::wstring();
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
    return wstrTo;
}

std::string wstring_to_utf8(const std::wstring& wstr) {
    if (wstr.empty()) return std::string();
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
}

//ИСПРАВЛЕННЫЕ ФУНКЦИИ ДЛЯ РАБОТЫ С ФАЙЛАМИ

/**
 * ИСПРАВЛЕННАЯ ФУНКЦИЯ: Определяет разделитель с использованием широких символов
 */
char detectDelimiter(const std::wstring& filename) {
    std::wifstream file(filename);
    if (!file.is_open()) {
        return ','; // По умолчанию запятая
    }

    // ВАЖНО: Устанавливаем локаль для корректного чтения UTF-8
    file.imbue(std::locale(std::locale::empty(), new std::codecvt_utf8<wchar_t>));

    std::vector<std::wstring> testLines;
    std::wstring line;
    int linesRead = 0;
    const int maxLinesToTest = 10;

    // Читаем несколько строк для анализа
    while (std::getline(file, line) && linesRead < maxLinesToTest) {
        if (!line.empty()) {
            testLines.push_back(line);
            linesRead++;
        }
    }
    file.close();

    if (testLines.empty()) {
        return ',';
    }

    // Тестируем различные разделители
    std::vector<wchar_t> delimiters = { L';', L',', L'\t', L'|' };
    std::map<wchar_t, int> scores;

    for (wchar_t delim : delimiters) {
        scores[delim] = 0;
        std::vector<int> columnCounts;

        // Анализируем каждую строку
        for (const std::wstring& testLine : testLines) {
            int count = 0;
            bool inQuotes = false;

            for (wchar_t c : testLine) {
                if (c == L'"') {
                    inQuotes = !inQuotes;
                }
                else if (c == delim && !inQuotes) {
                    count++;
                }
            }

            if (count > 0) {
                columnCounts.push_back(count + 1);
            }
        }

        // Проверяем консистентность количества колонок
        if (!columnCounts.empty()) {
            int firstCount = columnCounts[0];
            bool consistent = true;

            for (int count : columnCounts) {
                if (count != firstCount) {
                    consistent = false;
                    break;
                }
            }

            if (consistent && firstCount > 1) {
                scores[delim] = firstCount * 100;
            }
            else {
                scores[delim] = firstCount;
            }
        }
    }

    // Находим лучший разделитель
    wchar_t bestDelimiter = L',';
    int bestScore = 0;

    for (const auto& pair : scores) {
        if (pair.second > bestScore) {
            bestScore = pair.second;
            bestDelimiter = pair.first;
        }
    }

    // Конвертируем обратно в char
    return (char)bestDelimiter;
}

/**
 * ИСПРАВЛЕННАЯ ФУНКЦИЯ: Разделение строк с поддержкой широких символов
 */
std::vector<std::string> splitCSVLine(const std::wstring& line, wchar_t delimiter) {
    std::vector<std::string> result;
    std::wstring current;
    bool inQuotes = false;

    for (size_t i = 0; i < line.length(); ++i) {
        wchar_t c = line[i];

        if (c == L'"') {
            if (inQuotes && i + 1 < line.length() && line[i + 1] == L'"') {
                current += L'"';
                ++i;
            }
            else {
                inQuotes = !inQuotes;
            }
        }
        else if (c == delimiter && !inQuotes) {
            // Убираем пробелы в начале и конце
            size_t start = current.find_first_not_of(L" \t\r\n");
            if (start != std::wstring::npos) {
                size_t end = current.find_last_not_of(L" \t\r\n");
                current = current.substr(start, end - start + 1);
            }
            else {
                current = L"";
            }
            // Конвертируем в UTF-8 для хранения
            result.push_back(wstring_to_utf8(current));
            current.clear();
        }
        else {
            current += c;
        }
    }

    // Добавляем последнее поле
    size_t start = current.find_first_not_of(L" \t\r\n");
    if (start != std::wstring::npos) {
        size_t end = current.find_last_not_of(L" \t\r\n");
        current = current.substr(start, end - start + 1);
    }
    else {
        current = L"";
    }
    result.push_back(wstring_to_utf8(current));

    return result;
}

/**
 * Получает название разделителя для отображения пользователю
 */
std::wstring getDelimiterName(char delimiter) {
    switch (delimiter) {
    case ',': return L"запятая (,)";
    case ';': return L"точка с запятой (;)";
    case '\t': return L"табуляция";
    case '|': return L"вертикальная черта (|)";
    default: return L"неизвестный (" + std::wstring(1, delimiter) + L")";
    }
}

//МАТЕМАТИЧЕСКИЕ ФУНКЦИИ ДЛЯ C4.5
double calculateEntropy(const std::vector<int>& values) {
    if (values.empty()) return 0.0;

    std::map<int, int> counts;
    for (int val : values) {
        counts[val]++;
    }

    double entropy = 0.0;
    int total = values.size();

    for (const auto& pair : counts) {
        double probability = (double)pair.second / total;
        if (probability > 0) {
            entropy -= probability * log2(probability);
        }
    }
    return entropy;
}

double calculateSplitInformation(int leftSize, int rightSize) {
    int totalSize = leftSize + rightSize;
    if (totalSize == 0) return 0.0;

    double splitInfo = 0.0;

    if (leftSize > 0) {
        double leftRatio = (double)leftSize / totalSize;
        splitInfo -= leftRatio * log2(leftRatio);
    }

    if (rightSize > 0) {
        double rightRatio = (double)rightSize / totalSize;
        splitInfo -= rightRatio * log2(rightRatio);
    }

    return splitInfo;
}

double calculateGainRatio(double informationGain, double splitInformation) {
    if (splitInformation == 0.0 || splitInformation < 1e-10) {
        return 0.0;
    }
    return informationGain / splitInformation;
}

int getMajorityClass(const std::vector<int>& values) {
    if (values.empty()) return -1;

    std::map<int, int> counts;
    for (int val : values) {
        counts[val]++;
    }

    int majorityClass = values[0];
    int maxCount = 0;
    for (const auto& pair : counts) {
        if (pair.second > maxCount) {
            maxCount = pair.second;
            majorityClass = pair.first;
        }
    }
    return majorityClass;
}

//ИСПРАВЛЕННЫЕ ФУНКЦИИ ДЛЯ РАБОТЫ С CSV ФАЙЛАМИ

/**
 * ГЛАВНАЯ ИСПРАВЛЕННАЯ ФУНКЦИЯ: Парсинг CSV с использованием широких символов
 */
bool parseCSV(const std::wstring& filename) {
    // Сначала определяем разделитель
    detectedDelimiter = detectDelimiter(filename);
    wchar_t wideDelimiter = (wchar_t)detectedDelimiter;

    // ИСПРАВЛЕНО: Используем wifstream для работы с широкими символами
    std::wifstream file(filename);
    if (!file.is_open()) {
        // Дополнительная диагностика
        std::wstring errorMsg = L"Не удалось открыть файл:\n" + filename +
            L"\n\nПроверьте:\n• Существует ли файл\n• Нет ли русских символов в пути\n• Файл не заблокирован другой программой";
        MessageBox(hMainWindow, errorMsg.c_str(), L"Ошибка открытия файла", MB_OK | MB_ICONERROR);
        return false;
    }

    // ВАЖНО: Устанавливаем локаль для корректного чтения UTF-8
    file.imbue(std::locale(std::locale::empty(), new std::codecvt_utf8<wchar_t>));

    // Очищаем предыдущие данные
    csvData.clear();
    columnNames.clear();

    std::wstring line;
    bool firstLine = true;
    int lineNumber = 0;

    // Читаем файл построчно
    while (std::getline(file, line)) {
        lineNumber++;

        // Пропускаем пустые строки
        if (line.empty() || line.find_first_not_of(L" \t\r\n") == std::wstring::npos) {
            continue;
        }

        // Разбиваем строку с использованием определенного разделителя
        std::vector<std::string> row = splitCSVLine(line, wideDelimiter);

        if (firstLine) {
            columnNames = row;
            firstLine = false;

            // Диагностика заголовков
            std::wstring debugInfo = L"Файл успешно открыт!\n\n";
            MessageBox(hMainWindow, debugInfo.c_str(), L"Диагностика CSV", MB_OK | MB_ICONINFORMATION);
        }
        else {
            // Проверяем соответствие количества колонок
            if (row.size() != columnNames.size()) {
                std::wstring warning = L"Предупреждение: строка " + std::to_wstring(lineNumber) +
                    L" содержит " + std::to_wstring(row.size()) + L" колонок, ожидалось " +
                    std::to_wstring(columnNames.size());

                // Дополняем недостающие колонки пустыми значениями
                while (row.size() < columnNames.size()) {
                    row.push_back("");
                }
                // Или обрезаем лишние
                if (row.size() > columnNames.size()) {
                    row.resize(columnNames.size());
                }
            }
            csvData.push_back(row);
        }
    }

    file.close();

    if (columnNames.empty()) {
        MessageBox(hMainWindow, L"Файл не содержит заголовков!", L"Ошибка", MB_OK | MB_ICONERROR);
        return false;
    }

    if (csvData.empty()) {
        MessageBox(hMainWindow, L"Файл не содержит данных!", L"Ошибка", MB_OK | MB_ICONERROR);
        return false;
    }

    return true;
}

bool isNumericColumn(int columnIndex) {
    if (columnIndex >= columnNames.size()) return false;

    int numericCount = 0;
    int totalCount = 0;

    for (const auto& row : csvData) {
        if (columnIndex < row.size() && !row[columnIndex].empty()) {
            totalCount++;
            try {
                std::stod(row[columnIndex]);
                numericCount++;
            }
            catch (...) {
                // Не числовое значение
            }
        }
    }

    return totalCount > 0 && (double)numericCount / totalCount > 0.8;
}

//ФУНКЦИИ ПОДГОТОВКИ ДАННЫХ
DataSubset createDataSubset(const std::vector<int>& rowIndices, int yIndex, const std::vector<int>& numericColumns) {
    DataSubset subset;
    subset.originalRowIndices = rowIndices;
    subset.attributeValues.resize(numericColumns.size());

    for (int rowIdx : rowIndices) {
        if (rowIdx < csvData.size()) {
            const auto& row = csvData[rowIdx];

            if (yIndex < row.size()) {
                try {
                    int yVal = std::stoi(row[yIndex]);
                    subset.yValues.push_back(yVal);
                }
                catch (...) {
                    continue;
                }
            }

            for (size_t i = 0; i < numericColumns.size(); ++i) {
                int colIdx = numericColumns[i];
                if (colIdx < row.size()) {
                    try {
                        double attrVal = std::stod(row[colIdx]);
                        subset.attributeValues[i].push_back(attrVal);
                    }
                    catch (...) {
                        subset.attributeValues[i].push_back(0.0);
                    }
                }
            }
        }
    }

    return subset;
}

//СТРУКТУРА ДЛЯ РЕЗУЛЬТАТА ПОИСКА РАЗДЕЛЕНИЯ C4.5
struct SplitResult {
    int bestAttributeIndex;
    double bestThreshold;
    double bestInformationGain;
    double bestSplitInformation;
    double bestGainRatio;

    std::vector<int> leftIndices;
    std::vector<int> rightIndices;
    std::vector<int> leftY;
    std::vector<int> rightY;
    std::wstring detailedSteps;
};

//ОСНОВНАЯ ФУНКЦИЯ ПОИСКА ЛУЧШЕГО РАЗДЕЛЕНИЯ C4.5
SplitResult findBestSplit(const DataSubset& data, const std::vector<int>& numericColumns, int depth) {
    SplitResult result;
    result.bestGainRatio = -1.0;
    result.bestInformationGain = -1.0;
    result.bestSplitInformation = 0.0;
    result.bestAttributeIndex = -1;

    std::wostringstream steps;
    std::wstring indent = std::wstring(depth * 2, L' ');

    steps << indent << L"=== ПОИСК ЛУЧШЕГО РАЗДЕЛЕНИЯ ===\n";
    steps << indent << L"Глубина: " << depth << L"\n\n";

    double originalEntropy = calculateEntropy(data.yValues);
    steps << indent << L"Исходная энтропия: "
        << std::fixed << std::setprecision(4) << originalEntropy << L"\n";
    steps << indent << L"Количество образцов: " << data.yValues.size() << L"\n";

    // Показываем распределение классов
    steps << indent << L"Распределение классов:\n";
    std::map<int, int> classCounts;
    for (int y : data.yValues) {
        classCounts[y]++;
    }
    for (const auto& pair : classCounts) {
        steps << indent << L"  класс " << pair.first
            << L": " << pair.second << L" образцов\n";
    }
    steps << L"\n";

    //ПЕРЕБОР ВСЕХ АТРИБУТОВ
    for (size_t attrIdx = 0; attrIdx < numericColumns.size(); ++attrIdx) {
        int columnIndex = numericColumns[attrIdx];
        std::string attributeName = columnNames[columnIndex];

        steps << indent << L"--- Анализ атрибута: "
            << utf8_to_wstring(attributeName) << L" ---\n";

        const auto& attributeValues = data.attributeValues[attrIdx];

        if (attributeValues.size() != data.yValues.size()) {
            steps << indent << L"Ошибка: несоответствие размеров данных\n\n";
            continue;
        }

        std::vector<double> sortedValues = attributeValues;
        std::sort(sortedValues.begin(), sortedValues.end());
        sortedValues.erase(std::unique(sortedValues.begin(), sortedValues.end()), sortedValues.end());

        if (sortedValues.size() < 2) {
            steps << indent << L"Недостаточно уникальных значений\n\n";
            continue;
        }

        //ПЕРЕБОР ВСЕХ ВОЗМОЖНЫХ ПОРОГОВ
        for (size_t i = 0; i < sortedValues.size() - 1; ++i) {
            double threshold = (sortedValues[i] + sortedValues[i + 1]) / 2.0;

            std::vector<int> leftIndices, rightIndices;
            std::vector<int> leftY, rightY;

            for (size_t j = 0; j < attributeValues.size(); ++j) {
                if (attributeValues[j] < threshold) {
                    leftIndices.push_back(data.originalRowIndices[j]);
                    leftY.push_back(data.yValues[j]);
                }
                else {
                    rightIndices.push_back(data.originalRowIndices[j]);
                    rightY.push_back(data.yValues[j]);
                }
            }

            if (leftY.empty() || rightY.empty()) continue;

            //ВЫЧИСЛЕНИЕ МЕТРИК C4.5
            double leftEntropy = calculateEntropy(leftY);
            double rightEntropy = calculateEntropy(rightY);

            int totalSize = data.yValues.size();
            int leftSize = leftY.size();
            int rightSize = rightY.size();

            // Information Gain
            double weightedEntropy = ((double)leftSize / totalSize) * leftEntropy +
                ((double)rightSize / totalSize) * rightEntropy;
            double informationGain = originalEntropy - weightedEntropy;

            // Split Information
            double splitInformation = calculateSplitInformation(leftSize, rightSize);

            // Gain Ratio
            double gainRatio = calculateGainRatio(informationGain, splitInformation);

            // Подробное логирование
            steps << indent << L"Порог " << std::fixed << std::setprecision(2) << threshold << L":\n";
            steps << indent << L"  Information Gain = " << std::fixed << std::setprecision(4) << informationGain << L"\n";
            steps << indent << L"  Split Information = " << std::fixed << std::setprecision(4) << splitInformation << L"\n";
            steps << indent << L"  Gain Ratio = " << std::fixed << std::setprecision(4) << gainRatio << L"\n";
            steps << indent << L"  Левая ветвь: " << leftSize << L" образцов (энтропия: "
                << std::fixed << std::setprecision(4) << leftEntropy << L")\n";
            steps << indent << L"  Правая ветвь: " << rightSize << L" образцов (энтропия: "
                << std::fixed << std::setprecision(4) << rightEntropy << L")\n";

            if (gainRatio > result.bestGainRatio) {
                result.bestGainRatio = gainRatio;
                result.bestInformationGain = informationGain;
                result.bestSplitInformation = splitInformation;
                result.bestAttributeIndex = columnIndex;
                result.bestThreshold = threshold;
                result.leftIndices = leftIndices;
                result.rightIndices = rightIndices;
                result.leftY = leftY;
                result.rightY = rightY;

                steps << indent << L"  !!!НОВЫЙ ЛУЧШИЙ РЕЗУЛЬТАТ!!!\n";
            }
            steps << L"\n";
        }
    }

    //ВЫВОД ИТОГОВОГО РЕЗУЛЬТАТА
    if (result.bestGainRatio > 0) {
        steps << indent << L"ЛУЧШЕЕ РАЗДЕЛЕНИЕ:\n";
        steps << indent << L"Атрибут: "
            << utf8_to_wstring(columnNames[result.bestAttributeIndex]) << L"\n";
        steps << indent << L"Порог: "
            << std::fixed << std::setprecision(2) << result.bestThreshold << L"\n";
        steps << indent << L"Information Gain: "
            << std::fixed << std::setprecision(4) << result.bestInformationGain << L"\n";
        steps << indent << L"Split Information: "
            << std::fixed << std::setprecision(4) << result.bestSplitInformation << L"\n";
        steps << indent << L"Gain Ratio: "
            << std::fixed << std::setprecision(4) << result.bestGainRatio << L"\n";
        steps << indent << L"Левая ветвь: " << result.leftY.size()
            << L" образцов, энтропия: "
            << std::fixed << std::setprecision(4) << calculateEntropy(result.leftY) << L"\n";
        steps << indent << L"Правая ветвь: " << result.rightY.size()
            << L" образцов, энтропия: "
            << std::fixed << std::setprecision(4) << calculateEntropy(result.rightY) << L"\n";
    }
    else {
        steps << indent << L"Не найдено подходящего разделения\n";
    }

    result.detailedSteps = steps.str();
    return result;
}

//РЕКУРСИВНАЯ ФУНКЦИЯ ПОСТРОЕНИЯ ДЕРЕВА C4.5
std::unique_ptr<DecisionNode> buildDecisionTree(const DataSubset& data, const std::vector<int>& numericColumns,
    int depth, std::wostringstream& treeLog) {

    auto node = std::make_unique<DecisionNode>();
    node->depth = depth;
    node->yValues = data.yValues;
    node->entropy = calculateEntropy(data.yValues);
    node->predictedClass = getMajorityClass(data.yValues);

    std::wstring indent = std::wstring(depth * 2, L' ');

    treeLog << indent << L"УЗЕЛ НА ГЛУБИНЕ " << depth << L":\n";
    treeLog << indent << L"Образцов: " << data.yValues.size() << L"\n";
    treeLog << indent << L"Энтропия: "
        << std::fixed << std::setprecision(4) << node->entropy << L"\n";

    //УСЛОВИЯ ОСТАНОВКИ
    if (node->entropy == 0.0 || data.yValues.size() < 2 || depth >= 10) {
        node->isLeaf = true;
        treeLog << indent << L"ЛИСТ: Предсказанный класс = " << node->predictedClass;

        if (node->entropy == 0.0) {
            treeLog << L" (чистое разделение)\n";
        }
        else if (data.yValues.size() < 2) {
            treeLog << L" (недостаточно образцов)\n";
        }
        else {
            treeLog << L" (достигнута максимальная глубина)\n";
        }

        node->nodeDescription = L"Лист: класс " + std::to_wstring(node->predictedClass);
        return node;
    }

    //ПОИСК ЛУЧШЕГО РАЗДЕЛЕНИЯ ПО C4.5
    SplitResult split = findBestSplit(data, numericColumns, depth);
    treeLog << split.detailedSteps;

    if (split.bestGainRatio <= 0) {
        node->isLeaf = true;
        treeLog << indent << L"ЛИСТ: Предсказанный класс = " << node->predictedClass
            << L" (нет улучшения по Gain Ratio)\n";
        node->nodeDescription = L"Лист: класс " + std::to_wstring(node->predictedClass);
        return node;
    }

    //СОЗДАНИЕ ВНУТРЕННЕГО УЗЛА
    node->attributeIndex = split.bestAttributeIndex;
    node->attributeName = columnNames[split.bestAttributeIndex];
    node->threshold = split.bestThreshold;
    node->informationGain = split.bestInformationGain;
    node->splitInformation = split.bestSplitInformation;
    node->gainRatio = split.bestGainRatio;

    node->nodeDescription = utf8_to_wstring(node->attributeName) + L" < " +
        std::to_wstring(node->threshold).substr(0, 5);

    treeLog << indent << L"ВНУТРЕННИЙ УЗЕЛ:\n";
    treeLog << indent << L"Условие: " << utf8_to_wstring(node->attributeName)
        << L" < " << std::fixed << std::setprecision(2) << node->threshold << L"\n";
    treeLog << indent << L"Gain Ratio: " << std::fixed << std::setprecision(4) << node->gainRatio << L"\n\n";

    //РЕКУРСИВНОЕ ПОСТРОЕНИЕ ПОДДЕРЕВЬЕВ
    if (!split.leftY.empty()) {
        treeLog << indent << L"СТРОИМ ЛЕВОЕ ПОДДЕРЕВО:\n";
        DataSubset leftData = createDataSubset(split.leftIndices,
            std::find(columnNames.begin(), columnNames.end(), "Y") - columnNames.begin(),
            numericColumns);
        node->leftChild = buildDecisionTree(leftData, numericColumns, depth + 1, treeLog);
    }

    if (!split.rightY.empty()) {
        treeLog << indent << L"СТРОИМ ПРАВОЕ ПОДДЕРЕВО:\n";
        DataSubset rightData = createDataSubset(split.rightIndices,
            std::find(columnNames.begin(), columnNames.end(), "Y") - columnNames.begin(),
            numericColumns);
        node->rightChild = buildDecisionTree(rightData, numericColumns, depth + 1, treeLog);
    }

    return node;
}

//ФУНКЦИЯ ВИЗУАЛИЗАЦИИ ДЕРЕВА C4.5
std::wstring printTree(const DecisionNode* node, const std::wstring& prefix = L"", bool isLast = true) {
    if (!node) return L"";

    std::wostringstream result;
    result << prefix;
    result << (isLast ? L"└── " : L"├── ");

    if (node->isLeaf) {
        result << L"ЛИСТ: Класс " << node->predictedClass << L"\n";
        result << prefix << (isLast ? L"    " : L"│   ")
            << L"(энтропия: " << std::fixed << std::setprecision(4) << node->entropy
            << L", образцов: " << node->yValues.size() << L")\n";
    }
    else {
        result << utf8_to_wstring(node->attributeName) << L" < "
            << std::fixed << std::setprecision(2) << node->threshold << L"\n";
        result << prefix << (isLast ? L"    " : L"│   ")
            << L"(Gain Ratio: " << std::fixed << std::setprecision(4) << node->gainRatio
            << L", IG: " << std::fixed << std::setprecision(4) << node->informationGain << L")\n";

        std::wstring newPrefix = prefix + (isLast ? L"    " : L"│   ");
        if (node->leftChild) {
            result << printTree(node->leftChild.get(), newPrefix, !node->rightChild);
        }
        if (node->rightChild) {
            result << printTree(node->rightChild.get(), newPrefix, true);
        }
    }

    return result.str();
}

//ГЛАВНАЯ ФУНКЦИЯ АНАЛИЗА C4.5
void performAnalysis() {
    if (csvData.empty() || columnNames.empty()) {
        MessageBox(hMainWindow, L"Сначала загрузите CSV файл!", L"Ошибка", MB_OK | MB_ICONWARNING);
        return;
    }

    //ПОИСК ЦЕЛЕВОЙ ПЕРЕМЕННОЙ
    int yIndex = -1;
    for (size_t i = 0; i < columnNames.size(); ++i) {
        if (columnNames[i] == "Y" || columnNames[i] == "y") {
            yIndex = i;
            break;
        }
    }

    if (yIndex == -1) {
        MessageBox(hMainWindow, L"Не найден столбец 'Y' в данных!", L"Ошибка", MB_OK | MB_ICONERROR);
        return;
    }

    //ОПРЕДЕЛЕНИЕ ЧИСЛОВЫХ АТРИБУТОВ
    std::vector<int> numericColumns;
    for (size_t i = 0; i < columnNames.size(); ++i) {
        if ((int)i != yIndex && isNumericColumn(i)) {
            numericColumns.push_back(i);
        }
    }

    if (numericColumns.empty()) {
        MessageBox(hMainWindow, L"Не найдено числовых столбцов для анализа!", L"Ошибка", MB_OK | MB_ICONERROR);
        return;
    }

    //ФОРМИРОВАНИЕ ОТЧЕТА
    std::wostringstream results;
    // Информация о разделителе
    results << L"Информация о файле:\n";
    results << L"Обнаруженный разделитель: " << getDelimiterName(detectedDelimiter) << L"\n";
    results << L"Количество строк: " << csvData.size() << L"\n";
    results << L"Количество столбцов: " << columnNames.size() << L"\n";
    results << L"Числовые атрибуты:\n";
    for (size_t i = 0; i < numericColumns.size(); ++i) {
        results << L"  " << utf8_to_wstring(columnNames[numericColumns[i]]) << L"\n";
    }
    results << L"Целевой столбец: Y\n\n";

    //ПОСТРОЕНИЕ ДЕРЕВА
    std::vector<int> allIndices;
    for (size_t i = 0; i < csvData.size(); ++i) {
        allIndices.push_back(i);
    }

    DataSubset rootData = createDataSubset(allIndices, yIndex, numericColumns);

    results << L"=== ДЕТАЛЬНЫЙ ПРОЦЕСС ПОСТРОЕНИЯ ДЕРЕВА ===\n\n";

    std::wostringstream treeLog;
    auto decisionTree = buildDecisionTree(rootData, numericColumns, 0, treeLog);

    results << treeLog.str();
    results << L"\n=== ИТОГОВОЕ ДЕРЕВО РЕШЕНИЙ ===\n\n";
    results << printTree(decisionTree.get());

    resultsText = results.str();
    SetWindowText(hResultsText, resultsText.c_str());
    EnableWindow(hSaveButton, TRUE);
}

//ФУНКЦИИ ПОЛЬЗОВАТЕЛЬСКОГО ИНТЕРФЕЙСА
void saveResults() {
    if (resultsText.empty()) {
        MessageBox(hMainWindow, L"Нет результатов для сохранения!", L"Ошибка", MB_OK | MB_ICONWARNING);
        return;
    }

    OPENFILENAME ofn;
    wchar_t szFile[260] = L"дерево_решений.txt";
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hMainWindow;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = L"Текстовые файлы\0*.txt\0Все файлы\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;

    if (GetSaveFileName(&ofn)) {
        std::wofstream file(szFile);
        if (file.is_open()) {
            file.imbue(std::locale(std::locale::empty(), new std::codecvt_utf8<wchar_t>));
            file << resultsText;
            file.close();
            MessageBox(hMainWindow, L"Дерево решений успешно сохранено!", L"Успех", MB_OK | MB_ICONINFORMATION);
        }
        else {
            MessageBox(hMainWindow, L"Не удалось сохранить файл!", L"Ошибка", MB_OK | MB_ICONERROR);
        }
    }
}

/**
 * ИСПРАВЛЕННАЯ ФУНКЦИЯ: Загрузка CSV файла с правильной обработкой путей
 */
void loadCSVFile() {
    OPENFILENAME ofn;
    wchar_t szFile[260] = { 0 };
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hMainWindow;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = L"CSV файлы (все разделители)\0*.csv\0Текстовые файлы\0*.txt\0Все файлы\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    if (GetOpenFileName(&ofn)) {
        // ИСПРАВЛЕНО: Передаем широкую строку напрямую, без конвертации
        std::wstring filename(szFile);

        if (parseCSV(filename)) {
            SendMessage(hListBox, LB_RESETCONTENT, 0, 0);
            for (const auto& colName : columnNames) {
                std::wstring wideColName = utf8_to_wstring(colName);
                SendMessage(hListBox, LB_ADDSTRING, 0, (LPARAM)wideColName.c_str());
            }

            EnableWindow(hCalculateButton, TRUE);

            std::wstring message = L"Файл успешно загружен!\n";
            message += L"Путь: " + filename + L"\n";
            message += L"Разделитель: " + getDelimiterName(detectedDelimiter) + L"\n";
            message += L"Строк данных: " + std::to_wstring(csvData.size()) + L"\n";
            message += L"Столбцов: " + std::to_wstring(columnNames.size());
            MessageBox(hMainWindow, message.c_str(), L"Успех", MB_OK | MB_ICONINFORMATION);
        }
    }
}

//ОКОННАЯ ПРОЦЕДУРА
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE:
        hLoadButton = CreateWindow(L"BUTTON", L"Загрузить CSV",
            WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
            20, 20, 120, 30, hwnd, (HMENU)ID_LOAD_BUTTON,
            GetModuleHandle(NULL), NULL);

        hCalculateButton = CreateWindow(L"BUTTON", L"Построить дерево",
            WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | WS_DISABLED,
            160, 20, 160, 30, hwnd, (HMENU)ID_CALCULATE_BUTTON,
            GetModuleHandle(NULL), NULL);

        hSaveButton = CreateWindow(L"BUTTON", L"Сохранить дерево",
            WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | WS_DISABLED,
            340, 20, 130, 30, hwnd, (HMENU)ID_SAVE_BUTTON,
            GetModuleHandle(NULL), NULL);

        hListBox = CreateWindow(L"LISTBOX", NULL,
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_STANDARD,
            20, 70, 200, 150, hwnd, (HMENU)ID_LISTBOX,
            GetModuleHandle(NULL), NULL);

        hResultsText = CreateWindow(L"EDIT", L"Здесь будет отображено дерево решений. Корректное отображение при загрузке .txt",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_READONLY,
            240, 70, 540, 400, hwnd, (HMENU)ID_RESULTS_TEXT,
            GetModuleHandle(NULL), NULL);
        break;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_LOAD_BUTTON:
            loadCSVFile();
            break;
        case ID_CALCULATE_BUTTON:
            performAnalysis();
            break;
        case ID_SAVE_BUTTON:
            saveResults();
            break;
        }
        break;

    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED) {
            int width = LOWORD(lParam);
            int height = HIWORD(lParam);
            MoveWindow(hResultsText, 240, 70, width - 260, height - 90, TRUE);
        }
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

//ГЛАВНАЯ ФУНКЦИЯ ПРИЛОЖЕНИЯ
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_LISTVIEW_CLASSES;
    InitCommonControlsEx(&icex);

    const wchar_t CLASS_NAME[] = L"DecisionTreeBuilderC45FileFixed";

    WNDCLASS wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);

    RegisterClass(&wc);

    hMainWindow = CreateWindowEx(
        0,
        CLASS_NAME,
        L"Построитель дерева решений",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        1000, 700,
        NULL,
        NULL,
        hInstance,
        NULL
    );

    if (hMainWindow == NULL) {
        return 0;
    }

    ShowWindow(hMainWindow, nCmdShow);
    UpdateWindow(hMainWindow);

    MSG msg = {};
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}