# Programación y funcionamiento (revisión 2026-11-16)

## Editor de bloques

Desactivar las reglas y detener la cabina antes de editar. Cada bloque conserva
su entrada GPIO, nivel activo, salida, valor y reposo; ahora también tiene:

- Condición: GPIO, botón SUBIR, botón BAJAR, botón CABINA, en piso, subiendo,
  bajando, puertas abiertas o llamada pendiente.
- Piso: cualquier piso o uno específico. Para botones es el piso de la
  botonera; para GPIO/dirección/puertas restringe a la posición confirmada.
- Acción: escribir GPIO o registrar una llamada interior/exterior con destino.
  El destino es obligatorio para llamadas; no se usa la salida GPIO en ese caso.

Ejemplos (elegir pines realmente libres en el cableado):

| SI | Piso | Nivel | ENTONCES | Reposo |
|---|---|---|---|---|
| Botón SUBIR | 2 | 1 | GPIO 25 = 1 | 0 |
| Botón CABINA | 3 | 1 | GPIO 27 = 1 | 0 |
| GPIO 28 | cualquiera | 1 | Pedir cabina, destino 2 | no aplica |
| Subiendo | cualquiera | 1 | GPIO 49 = 1 | 0 |
| Bajando | cualquiera | 1 | GPIO 50 = 1 | 0 |

Los botones virtuales producen pulsos de **300 ms**, incluso si la cabina ya
está en ese piso o la llamada está repetida. No se asigna automáticamente un
GPIO: se elige la salida mediante un bloque. Para mantener una señal hasta que
se atienda el pedido, usar «Llamada pendiente». Las acciones de llamada se
procesan al pasar de condición falsa a verdadera, evitando repetirlas cada 10 ms.
Los GPIO físicos se filtran 30 ms. Los sensores de piso se muestrean cada 100 ms;
la confirmación requiere muestras estables y puede tardar unos 200 ms.

Varias condiciones de salida se combinan mediante OR: basta una condición
activa para escribir el valor activo. Todas deben compartir valor activo y
reposo. No se permiten pines del panel ni salidas sobre sensores de piso.
El esquema NVS pasa a `rules_v2`; los bloques `rules_v1` se leen como reglas
GPIO y conservan sus notas. Un esquema desconocido se rechaza.

## Recorrido colectivo y pantalla

Se guardan por separado las llamadas interiores, exteriores de subida y de
bajada, con hasta 16 pisos y 32 bloques en total. Durante el recorrido se evalúa
cada piso confirmado, incorporando pedidos nuevos sin esperar al destino inicial.
Las llamadas interiores y las exteriores del mismo sentido provocan parada.
Las exteriores del sentido contrario se conservan para el regreso, salvo en
el extremo del recorrido pendiente, donde puede invertirse el sentido.

No se admiten BAJAR en el piso inferior ni SUBIR en el superior. Un pedido en
el piso actual detenido vuelve a iniciar el tiempo de puertas. Se conserva el
sentido entre paradas para continuar la recogida. La vista **Técnica → Ver pisos**
es desplazable y muestra todos los pisos, sensor asignado, cabina confirmada y
llamadas C/S/B. La botonera interior también distingue posición y pedidos.

La posición solo procede de sensores. No hay avance por temporizador ni opción
de modo demo; la clave/los campos antiguos se conservan inactivos por compatibilidad.
Sin un sensor confirmado no se inicia una ruta. Varios sensores simultáneos
registran un fallo retenido, detienen la lógica de movimiento y ponen las reglas
en reposo. Corregir las señales, desactivar bloques y reaplicar configuración para
reinicializar. Entre pisos no se inventa una posición.

## Qué falta respecto a un ascensor real

El editor sigue siendo un motor de reglas acotado, no un PLC general: faltan
expresiones AND/OR arbitrarias, temporizadores configurables, memorias y secuencias
programables. El recorrido colectivo continúa siendo parte del controlador C.

La variable de puertas sigue siendo una **orden lógica con temporizador de 3 s**;
no confirma apertura, cierre ni enclavamiento físicos. Para convertirlo en control
de una instalación hacen falta señales y decisiones de hardware concretas:

| Función pendiente | Información / implementación necesaria |
|---|---|
| Puertas | Contactos de cierre y enclavamiento, fotocélula, abrir/cerrar, obstrucción y tiempos máximos |
| Movimiento | Contactores mutuamente enclavados, freno, realimentación y velocidad / nivelación |
| Límites | Finales superior/inferior independientes, coherencia y orden de sensores |
| Averías | Tiempo máximo entre pisos, fallo de sensor, fallo de contactor y recuperación supervisada |
| Carga y emergencia | Sobrecarga, cadena de seguridad, parada de emergencia, alarma y rescate |
| Servicio | Inspección, bomberos, prioridad, aparcamiento y tratamiento de corte de energía |
| E/S | Solo hay 11 GPIO J7; 16 sensores más botones y actuadores requieren ampliar E/S |

El reposo de cada salida es configurable; debe corresponder al circuito cableado.
Las reglas no reemplazan los enclavamientos eléctricos independientes. No se ha
verificado accionamiento físico, distancia de frenado ni comportamiento de puertas.

## Verificación

`bash tests/host/run.sh` compila el controlador y el motor reales con GPIO/reloj
de prueba y sanitizadores de memoria/comportamiento indefinido. Comprueba paradas
intermedias de ambas procedencias, llamadas contrarias retenidas incluso en un
piso servido por llamada interior, pulsos, GPIO físico → llamada, OR de salidas,
reposo, conflicto de sensores y límites de llamadas exteriores.

No valida LVGL en pantalla, NVS en la placa ni el firmware completo. Requiere
compilar con ESP-IDF y probar cableado y pantalla en la CrowPanel correspondiente.

## Configuracion cargada en la CrowPanel de tres pisos

| Boton de cabina | GPIO de salida | Entrada del PLC |
| --- | --- | --- |
| P01 | 52 | X000 |
| P02 | 51 | X001 |
| P03 | 50 | X002 |

Cada bloque usa **Programacion tipo Boton**, fuente **CABINA**, filtro por el
piso de destino y condicion **Al pulsar**. La salida se guarda con activo 1
y reposo 0. El pulso dura unos 300 ms. Los sensores de piso 1, 2 y 3 usan
GPIO 2, 3 y 4, respectivamente. Los bloques se configuraron en NVS en la
placa de prueba. En otra placa se pueden reproducir con esta tabla.

En Exterior e Interior, el sentido observado se obtiene comparando los dos
ultimos pisos distintos detectados. Hasta que hay una transicion verificable
se muestra **Sentido sin confirmar**. La prueba fisica de recorrido queda
pendiente.
