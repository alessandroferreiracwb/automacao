<?php
// config.php
define('DB_HOST', 'localhost');
define('DB_NAME', 'esp32_automacao');
define('DB_USER', 'root');
define('DB_PASS', ''); // XAMPP padrão é vazio
define('DB_CHARSET', 'utf8mb4');

function getDBConnection() {
    try {
        $pdo = new PDO(
            "mysql:host=" . DB_HOST . ";dbname=" . DB_NAME . ";charset=" . DB_CHARSET,
            DB_USER,
            DB_PASS,
            [PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION]
        );
        return $pdo;
    } catch (PDOException $e) {
        error_log("Erro DB: " . $e->getMessage());
        return null;
    }
}
?>