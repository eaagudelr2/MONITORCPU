#!/bin/bash

# Número de procesos que deseas crear
NUM_PROCESOS=1

# Función que consume CPU
consume_cpu() {
    local j
    for ((j=0; j<1000000; j++)); do
        result=$(echo "scale=10; 4*a(1)" | bc -l)
    done
}

# Crear múltiples procesos que consumen CPU
for i in $(seq 1 $NUM_PROCESOS); do
    consume_cpu &
    echo "Proceso $i creado."
done

# Esperar a que todos los procesos terminen (esto no se ejecutará ya que los procesos se ejecutan en segundo plano)
wait
