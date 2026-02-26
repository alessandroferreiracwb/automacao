<?php
// receive_log.php - COM DEBUG COMPLETO
error_reporting(E_ALL);
ini_set('display_errors', 0); // Não mostrar erros no navegador
header('Content-Type: application/json');
header('Access-Control-Allow-Origin: *');
header('Access-Control-Allow-Methods: POST, GET, OPTIONS');
header('Access-Control-Allow-Headers: Content-Type');

// Arquivo de log
$logFile = __DIR__ . '/debug.log';

function debugLog($msg) {
    global $logFile;
    $ts = date('Y-m-d H:i:s');
    @file_put_contents($logFile, "[$ts] $msg\n", FILE_APPEND);
}

debugLog("=== === === NOVA REQUISIÇÃO === === ===");
debugLog("REQUEST_METHOD: " . $_SERVER['REQUEST_METHOD']);
debugLog("REMOTE_ADDR: " . $_SERVER['REMOTE_ADDR']);
debugLog("CONTENT_TYPE: " . ($_SERVER['CONTENT_TYPE'] ?? 'não definido'));

// Aceitar POST
if ($_SERVER['REQUEST_METHOD'] !== 'POST') {
    debugLog("ERRO: Método não é POST");
    http_response_code(405);
    echo json_encode(['status' => 'error', 'message' => 'Metodo nao permitido']);
    exit();
}

// Ler input raw
$input = file_get_contents('php://input');
debugLog("INPUT RAW (" . strlen($input) . " bytes): " . $input);

// Verificar se input está vazio
if (empty($input)) {
    debugLog("ERRO: Input vazio");
    http_response_code(400);
    echo json_encode(['status' => 'error', 'message' => 'Input vazio']);
    exit();
}

// Decodificar JSON
$data = json_decode($input, true);
$jsonError = json_last_error();

if ($jsonError !== JSON_ERROR_NONE) {
    debugLog("ERRO JSON: " . json_last_error_msg());
    http_response_code(400);
    echo json_encode(['status' => 'error', 'message' => 'JSON invalido: ' . json_last_error_msg()]);
    exit();
}

debugLog("JSON DECODIFICADO COM SUCESSO");
debugLog("CHAVES DO ARRAY: " . implode(', ', array_keys($data)));

// Verificar se 'logs' existe e é array
if (!isset($data['logs']) || !is_array($data['logs'])) {
    debugLog("ERRO: 'logs' não existe ou não é array");
    debugLog("DATA COMPLETA: " . json_encode($data));
    http_response_code(400);
    echo json_encode(['status' => 'error', 'message' => 'Dados invalidos: logs ausente']);
    exit();
}

debugLog("LOGS ENCONTRADOS: " . count($data['logs']) . " registros");

// Verificar banco de dados
require_once 'config.php';
$pdo = getDBConnection();

if (!$pdo) {
    debugLog("ERRO: Conexão DB falhou");
    http_response_code(500);
    echo json_encode(['status' => 'error', 'message' => 'Erro no banco']);
    exit();
}

debugLog("CONEXÃO DB OK");

try {
    $stmt = $pdo->prepare("
        INSERT INTO logs (timestamp, datetime, relay, action, type, ip_address, esp_id) 
        VALUES (:timestamp, FROM_UNIXTIME(:timestamp), :relay, :action, :type, :ip, :esp_id)
    ");
    
    $esp_id = $data['esp_id'] ?? 'esp32_reles';
    $device_ip = $data['device_ip'] ?? '';
    $inserted = 0;
    $errors = 0;
    
    debugLog("ESP_ID: " . $esp_id);
    debugLog("DEVICE_IP: " . $device_ip);
    
    foreach ($data['logs'] as $index => $log) {
        debugLog("LOG #" . $index . ": " . json_encode($log));
        
        if (isset($log['ts'], $log['relay'], $log['act'], $log['type'], $log['ip'])) {
            $stmt->execute([
                ':timestamp' => $log['ts'],
                ':relay' => $log['relay'],
                ':action' => $log['act'],
                ':type' => $log['type'],
                ':ip' => $log['ip'],
                ':esp_id' => $esp_id
            ]);
            $inserted++;
            debugLog("LOG #" . $index . " INSERIDO");
        } else {
            $errors++;
            debugLog("LOG #" . $index . " IGNORADO (campos faltando)");
        }
    }
    
    // Atualizar dispositivo
    $espStmt = $pdo->prepare("
        INSERT INTO esp_devices (esp_id, ip_address, last_seen, total_logs) 
        VALUES (:esp_id, :ip, :last_seen, :logs)
        ON DUPLICATE KEY UPDATE ip_address=:ip, last_seen=:last_seen, total_logs=total_logs+:logs
    ");
    $espStmt->execute([
        ':esp_id' => $esp_id,
        ':ip' => $device_ip,
        ':last_seen' => time(),
        ':logs' => $inserted
    ]);
    
    debugLog("=== SUCESSO: {$inserted} logs inseridos, {$errors} erros ===");
    
    echo json_encode([
        'status' => 'ok',
        'message' => "{$inserted} logs registrados",
        'inserted' => $inserted,
        'errors' => $errors,
        'server_time' => date('Y-m-d H:i:s')
    ]);
    
} catch (PDOException $e) {
    debugLog("ERRO PDO: " . $e->getMessage());
    http_response_code(500);
    echo json_encode(['status' => 'error', 'message' => 'Erro PDO: ' . $e->getMessage()]);
}
?>