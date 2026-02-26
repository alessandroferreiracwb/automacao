<?php
// logs_dashboard.php - Visualizar logs com filtros
require_once 'config.php';
$pdo = getDBConnection();

if (!$pdo) {
    die("❌ Erro de conexão com o banco de dados");
}

// Filtros
$relay_filter = $_GET['relay'] ?? '';
$date_filter = $_GET['date'] ?? date('Y-m-d');
$type_filter = $_GET['type'] ?? '';
$limit = 200;

// === QUERY PRINCIPAL ===
$sql = "SELECT * FROM logs WHERE DATE(datetime) = :date";
$params = [
    ':date' => $date_filter,
    ':limit' => $limit  // ← ADICIONAR :limit AQUI
];

if ($relay_filter !== '') {
    $sql .= " AND relay = :relay";
    $params[':relay'] = $relay_filter;
}

if ($type_filter !== '') {
    $sql .= " AND type = :type";
    $params[':type'] = $type_filter;
}

$sql .= " ORDER BY timestamp DESC LIMIT :limit";

$stmt = $pdo->prepare($sql);

// Bind correto de todos os parâmetros
foreach ($params as $key => $value) {
    if ($key === ':limit') {
        $stmt->bindValue($key, (int)$value, PDO::PARAM_INT);
    } elseif (is_int($value)) {
        $stmt->bindValue($key, $value, PDO::PARAM_INT);
    } else {
        $stmt->bindValue($key, $value);
    }
}

$stmt->execute();
$logs = $stmt->fetchAll(PDO::FETCH_ASSOC);

// === ESTATÍSTICAS ===
$statsSql = "
    SELECT 
        COUNT(*) as total,
        SUM(CASE WHEN action=1 THEN 1 ELSE 0 END) as ligados,
        SUM(CASE WHEN action=0 THEN 1 ELSE 0 END) as desligados,
        COUNT(DISTINCT ip_address) as ips_unicos
    FROM logs 
    WHERE DATE(datetime) = :date
";

$statsStmt = $pdo->prepare($statsSql);
$statsStmt->execute([':date' => $date_filter]);
$stats = $statsStmt->fetch(PDO::FETCH_ASSOC);
?>
<!DOCTYPE html>
<html lang="pt-BR">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>📊 Histórico ESP32</title>
    <style>
        :root{--primary:#2196F3;--success:#4CAF50;--danger:#f44336;--bg:#f5f5f5;--card:#fff}
        *{box-sizing:border-box;margin:0;padding:0}
        body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;background:var(--bg);padding:20px}
        .container{max-width:1200px;margin:0 auto}
        h1{color:#333;margin-bottom:20px}
        .stats{display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,1fr));gap:15px;margin-bottom:20px}
        .stat-card{background:var(--card);padding:20px;border-radius:12px;box-shadow:0 2px 8px rgba(0,0,0,0.1)}
        .stat-value{font-size:28px;font-weight:bold;color:var(--primary)}
        .stat-label{color:#666;font-size:14px}
        .filters{background:var(--card);padding:20px;border-radius:12px;margin-bottom:20px;display:flex;gap:10px;flex-wrap:wrap;align-items:center}
        .filters select,.filters input{padding:10px;border:1px solid #ddd;border-radius:8px;font-size:14px}
        .filters button{background:var(--primary);color:white;padding:10px 20px;border:none;border-radius:8px;cursor:pointer}
        table{width:100%;background:var(--card);border-radius:12px;overflow:hidden;box-shadow:0 2px 8px rgba(0,0,0,0.1)}
        th,td{padding:12px;text-align:left;border-bottom:1px solid #eee}
        th{background:#f8f9fa;font-weight:600;color:#333}
        tr:hover{background:#f8f9fa}
        .badge{padding:4px 10px;border-radius:12px;font-size:11px;font-weight:700;display:inline-block}
        .badge-on{background:var(--success);color:white}
        .badge-off{background:var(--danger);color:white}
        .badge-manual{background:#FF9800;color:white}
        .badge-sched{background:#9C27B0;color:white}
        .ip{font-family:monospace;color:#666;font-size:12px}
        .no-logs{text-align:center;padding:40px;color:#666}
        .refresh{margin-left:auto}
        .error{background:#ffebee;color:#c62828;padding:15px;border-radius:8px;margin-bottom:20px}
    </style>
</head>
<body>
<div class="container">
    <h1>📊 Histórico de Logs - ESP32</h1>
    
    <div class="stats">
        <div class="stat-card">
            <div class="stat-value"><?= $stats['total'] ?? 0 ?></div>
            <div class="stat-label">Total de Logs</div>
        </div>
        <div class="stat-card">
            <div class="stat-value" style="color:var(--success)"><?= $stats['ligados'] ?? 0 ?></div>
            <div class="stat-label">Relés Ligados</div>
        </div>
        <div class="stat-card">
            <div class="stat-value" style="color:var(--danger)"><?= $stats['desligados'] ?? 0 ?></div>
            <div class="stat-label">Relés Desligados</div>
        </div>
        <div class="stat-card">
            <div class="stat-value"><?= $stats['ips_unicos'] ?? 0 ?></div>
            <div class="stat-label">IPs Únicos</div>
        </div>
    </div>
    
    <form class="filters" method="GET">
        <select name="relay">
            <option value="">Todos os Relés</option>
            <option value="1" <?= $relay_filter=='1'?'selected':'' ?>>Relé 1</option>
            <option value="2" <?= $relay_filter=='2'?'selected':'' ?>>Relé 2</option>
            <option value="3" <?= $relay_filter=='3'?'selected':'' ?>>Relé 3</option>
            <option value="4" <?= $relay_filter=='4'?'selected':'' ?>>Relé 4</option>
        </select>
        
        <input type="date" name="date" value="<?= $date_filter ?>">
        
        <select name="type">
            <option value="">Todos os Tipos</option>
            <option value="0" <?= $type_filter==='0'?'selected':'' ?>>Manual</option>
            <option value="1" <?= $type_filter==='1'?'selected':'' ?>>Agendado</option>
        </select>
        
        <button type="submit">🔍 Filtrar</button>
        <button type="button" class="refresh" onclick="window.location='logs_dashboard.php'">🔄 Limpar Filtros</button>
    </form>
    
    <table>
        <thead>
            <tr>
                <th>Data/Hora</th>
                <th>Relé</th>
                <th>Ação</th>
                <th>Tipo</th>
                <th>IP Origem</th>
                <th>ESP32</th>
            </tr>
        </thead>
        <tbody>
            <?php if (empty($logs)): ?>
            <tr><td colspan="6" class="no-logs">Nenhum log encontrado para <?= date('d/m/Y', strtotime($date_filter)) ?></td></tr>
            <?php else: ?>
                <?php foreach ($logs as $log): ?>
                <tr>
                    <td><?= date('d/m/Y H:i:s', $log['timestamp']) ?></td>
                    <td><strong>Relé <?= $log['relay'] ?></strong></td>
                    <td><span class="badge <?= $log['action']?'badge-on':'badge-off' ?>">
                        <?= $log['action']?'LIGADO':'DESLIGADO' ?>
                    </span></td>
                    <td><span class="badge <?= $log['type']?'badge-sched':'badge-manual' ?>">
                        <?= $log['type']?'⏰ Agendado':'🖐️ Manual' ?>
                    </span></td>
                    <td class="ip">👤 <?= $log['ip_address'] ?></td>
                    <td><?= $log['esp_id'] ?></td>
                </tr>
                <?php endforeach; ?>
            <?php endif; ?>
        </tbody>
    </table>
    
    <p style="text-align:center;margin-top:20px;color:#666;font-size:12px">
        Mostrando até <?= $limit ?> registros • Dados armazenados permanentemente
    </p>
</div>
</body>
</html>