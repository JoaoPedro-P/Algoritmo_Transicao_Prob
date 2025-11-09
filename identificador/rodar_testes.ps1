# --- 1. CONFIGURAÇÃO ---
$num_runs = 100
$cpp_executable = ".\genTest.exe"

# --- 2. VERIFICAÇÃO ---
if (-not (Test-Path $cpp_executable)) {
    Write-Host "Erro: Executável '$cpp_executable' não encontrado." -ForegroundColor Red
    Write-Host "Por favor, compile o programa C++ primeiro." -ForegroundColor Yellow
    exit
}

Write-Host "INFO: Iniciando o teste de performance com $num_runs execuções..."
Write-Host "--------------------------------------------------------"

# --- 3. EXECUÇÃO E COLETA DE DADOS ---
$run_times_seconds = New-Object System.Collections.Generic.List[double]

for ($i = 1; $i -le $num_runs; $i++) {
    $stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
    & $cpp_executable | Out-Null
    $stopwatch.Stop()
    
    $current_run_time_sec = $stopwatch.Elapsed.TotalSeconds
    $run_times_seconds.Add($current_run_time_sec)
    
    # ALTERADO: Exibe o tempo da iteração também em microssegundos.
    $current_run_time_us = $current_run_time_sec * 1000000
    Write-Host ("INFO: Execução {0} de {1} finalizada em {2:N2} microsegundos." -f $i, $num_runs, $current_run_time_us)
}

# --- 4. CÁLCULO E RESULTADO FINAL ---
Write-Host "--------------------------------------------------------"
Write-Host "INFO: Todas as execuções foram finalizadas. Calculando estatísticas..."

# --- Cálculos em segundos ---
$total_time_sec = ($run_times_seconds | Measure-Object -Sum).Sum
$average_time_sec = $total_time_sec / $num_runs

$sum_of_squared_diffs = 0.0
foreach ($time in $run_times_seconds) {
    $sum_of_squared_diffs += [Math]::Pow($time - $average_time_sec, 2)
}
$variance = $sum_of_squared_diffs / $num_runs
$std_dev_sec = [Math]::Sqrt($variance)

# ALTERADO: Conversão dos resultados finais para microssegundos
$average_time_us = $average_time_sec * 1000000
$std_dev_us = $std_dev_sec * 1000000

# --- Cálculo do Coeficiente de Variação (não muda, pois a unidade é cancelada) ---
$cv_percentage = 0.0
if ($average_time_sec -gt 0) {
    $cv_percentage = ($std_dev_sec / $average_time_sec) * 100
}

# --- Exibição dos Resultados ---
Write-Host " "
Write-Host "=================== RESULTADO FINAL ==================="
Write-Host ("Número de Execuções:       {0}" -f $num_runs)
# ALTERADO: Exibe os valores em microssegundos
Write-Host ("Tempo Médio de Execução:   {0:N2} microsegundos" -f $average_time_us)
Write-Host ("Desvio Padrão:             {0:N2} microsegundos" -f $std_dev_us)
Write-Host ("Coeficiente de Variação:   {0:N2} %" -f $cv_percentage)
Write-Host "====================================================="