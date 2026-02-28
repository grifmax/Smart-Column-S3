// Animated Scheme Controller for All Modes

class SchemeController {
    constructor() {
        this.currentMode = null;
        this.schemeContainer = document.querySelector('.scheme-container');

        // SVG paths для каждого режима
        this.schemePaths = {
            0: null, // IDLE - no scheme
            1: 'schemes/column-animated.svg', // RECT
            2: 'schemes/column-animated.svg', // MANUAL (same as RECT)
            3: 'schemes/distillation-animated.svg', // DIST
            4: 'schemes/mash-animated.svg', // MASH
            5: 'schemes/hold-animated.svg' // HOLD
        };

        this.controllers = {
            rect: null,
            dist: null,
            mash: null,
            hold: null
        };
    }

    // Переключить схему в зависимости от режима
    switchScheme(mode) {
        if (this.currentMode === mode) return;
        this.currentMode = mode;

        const schemePath = this.schemePaths[mode];
        if (!schemePath || !this.schemeContainer) return;

        // Загрузить новую схему
        const objectElement = this.schemeContainer.querySelector('object');
        if (objectElement) {
            objectElement.data = schemePath;

            // После загрузки инициализировать соответствующий контроллер
            objectElement.addEventListener('load', () => {
                this.initController(mode);
            }, { once: true });
        }
    }

    // Инициализировать контроллер для текущего режима
    initController(mode) {
        switch (mode) {
            case 1: // RECT
            case 2: // MANUAL
                this.controllers.rect = new RectController();
                break;
            case 3: // DIST
                this.controllers.dist = new DistController();
                break;
            case 4: // MASH
                this.controllers.mash = new MashController();
                break;
            case 5: // HOLD
                this.controllers.hold = new HoldController();
                break;
        }
    }

    // Обновить состояние
    updateState(data) {
        if (data.mode !== undefined) {
            this.switchScheme(data.mode);
        }

        // Передать данные соответствующему контроллеру
        switch (this.currentMode) {
            case 1:
            case 2:
                if (this.controllers.rect) {
                    this.controllers.rect.update(data);
                }
                break;
            case 3:
                if (this.controllers.dist) {
                    this.controllers.dist.update(data);
                }
                break;
            case 4:
                if (this.controllers.mash) {
                    this.controllers.mash.update(data);
                }
                break;
            case 5:
                if (this.controllers.hold) {
                    this.controllers.hold.update(data);
                }
                break;
        }
    }
}

// ========== RECT Controller (existing) ==========

class RectController {
    constructor() {
        this.cube = document.querySelector('#column-scheme .cube');
        this.powerBtn = document.querySelector('#column-scheme .power-btn');
        this.pumpWheel = document.querySelector('#column-scheme .pump-wheel');
    }

    update(data) {
        // Update based on phase
        if (data.phase !== undefined) {
            this.updatePhase(data.phase);
        }

        // Update temperatures
        if (data.t_cube !== undefined) {
            const tempCube = document.querySelector('#column-scheme #temp-cube');
            if (tempCube) tempCube.textContent = data.t_cube.toFixed(1) + '°C';
        }

        // Update jar volumes
        if (data.volume_heads !== undefined) {
            this.updateJar('heads', data.volume_heads);
        }
        if (data.volume_body !== undefined) {
            this.updateJar('body', data.volume_body);
        }
        if (data.volume_tails !== undefined) {
            this.updateJar('tails', data.volume_tails);
        }

        // Update pump
        if (data.pump_speed !== undefined) {
            this.updatePump(data.pump_speed);
        }
    }

    updatePhase(phase) {
        // 0=IDLE, 1=HEATING, 2=STABIL, 3=HEADS, 4=PURGE, 5=BODY, 6=TAILS
        if (phase === 0) {
            this.cube?.classList.remove('heating');
        } else {
            this.cube?.classList.add('heating');
        }

        if (phase >= 1) {
            this.powerBtn?.classList.add('active');
        } else {
            this.powerBtn?.classList.remove('active');
        }
    }

    updateJar(type, volume) {
        const liquid = document.querySelector(`#column-scheme #liquid-${type}`);
        const volText = document.querySelector(`#column-scheme #vol-${type}`);

        if (liquid) {
            const maxHeight = 100;
            const height = Math.min((volume / 2000) * maxHeight, maxHeight);
            liquid.setAttribute('height', height);
            liquid.setAttribute('y', maxHeight + 20 - height);
        }

        if (volText) {
            volText.textContent = Math.round(volume) + 'ml';
        }
    }

    updatePump(speed) {
        if (this.pumpWheel) {
            if (speed > 0) {
                this.pumpWheel.classList.add('running');
            } else {
                this.pumpWheel.classList.remove('running');
            }
        }
    }
}

// ========== DIST Controller ==========

class DistController {
    constructor() {
        this.cube = document.querySelector('#dist-scheme #dist-cube');
        this.powerBtn = document.querySelector('#dist-scheme #dist-power-btn');
    }

    update(data) {
        // Update temperatures
        if (data.t_cube !== undefined) {
            const temp = document.querySelector('#dist-scheme #dist-temp-cube');
            if (temp) temp.textContent = data.t_cube.toFixed(1) + '°C';

            // Heating animation
            if (data.t_cube > 80) {
                this.cube?.classList.add('heating');
            } else {
                this.cube?.classList.remove('heating');
            }
        }

        if (data.t_tsa !== undefined) {
            const temp = document.querySelector('#dist-scheme #dist-temp-vapor');
            if (temp) temp.textContent = data.t_tsa.toFixed(1) + '°C';
        }

        if (data.t_reflux !== undefined) {
            const temp = document.querySelector('#dist-scheme #dist-temp-product');
            if (temp) temp.textContent = data.t_reflux.toFixed(1) + '°C';
        }

        // Update jar volume
        if (data.pump_volume !== undefined) {
            this.updateJar(data.pump_volume);
        }

        // Update ABV
        if (data.abv !== undefined) {
            const abv = document.querySelector('#dist-scheme #dist-abv');
            if (abv) abv.textContent = data.abv.toFixed(1) + '%';
        }

        // Update power
        if (data.power !== undefined) {
            const power = document.querySelector('#dist-scheme #dist-power-level');
            if (power) power.textContent = Math.round(data.power) + '%';

            if (data.power > 0) {
                this.powerBtn?.classList.add('active');
            } else {
                this.powerBtn?.classList.remove('active');
            }
        }
    }

    updateJar(volume) {
        const liquid = document.querySelector('#dist-scheme #dist-liquid');
        const volText = document.querySelector('#dist-scheme #dist-volume');

        if (liquid) {
            const maxHeight = 180;
            const height = Math.min((volume / 5000) * maxHeight, maxHeight);
            liquid.setAttribute('height', height);
            liquid.setAttribute('y', 600 + (180 - height));
        }

        if (volText) {
            volText.textContent = Math.round(volume) + ' ml';
        }
    }
}

// ========== MASH Controller ==========

class MashController {
    constructor() {
        this.stirrer = document.querySelector('#mash-scheme .stirrer');
        this.powerBtn = document.querySelector('#mash-scheme #mash-power-btn');
        this.currentPause = 0; // 0=protein, 1=sac63, 2=sac72, 3=mashout
    }

    update(data) {
        // Update temperature
        if (data.t_cube !== undefined) {
            const tempText = document.querySelector('#mash-scheme #mash-temp-current');
            if (tempText) tempText.textContent = data.t_cube.toFixed(1) + '°C';

            // Update mercury column
            this.updateMercury(data.t_cube);
        }

        // Update stirrer speed
        if (data.pump_speed !== undefined) {
            const rpmText = document.querySelector('#mash-scheme #mash-stirrer-rpm');
            if (rpmText) rpmText.textContent = Math.round(data.pump_speed / 10) + ' RPM';

            if (data.pump_speed > 0) {
                this.stirrer?.classList.add('running');
            } else {
                this.stirrer?.classList.remove('running');
            }
        }

        // Update power level
        if (data.power !== undefined) {
            const powerText = document.querySelector('#mash-scheme #mash-power-level');
            if (powerText) powerText.textContent = Math.round(data.power) + '%';

            if (data.power > 0) {
                this.powerBtn?.classList.add('active');
            } else {
                this.powerBtn?.classList.remove('active');
            }
        }

        // Update timer (if available)
        // This would need timer data from backend
    }

    updateMercury(temp) {
        const mercury = document.querySelector('#mash-scheme #mash-mercury');
        if (!mercury) return;

        // Map temperature to mercury height (40-80°C range)
        const minTemp = 40;
        const maxTemp = 80;
        const maxHeight = 340;

        const ratio = Math.min(Math.max((temp - minTemp) / (maxTemp - minTemp), 0), 1);
        const height = ratio * maxHeight;

        mercury.setAttribute('height', height);
        mercury.setAttribute('y', 515 - height);
    }
}

// ========== HOLD Controller ==========

class HoldController {
    constructor() {
        this.powerBtn = document.querySelector('#hold-scheme #hold-power-btn');
        this.targetTemp = 75; // Default target temperature
    }

    update(data) {
        // Update temperature
        if (data.t_cube !== undefined) {
            const tempText = document.querySelector('#hold-scheme #hold-temp-value');
            if (tempText) tempText.textContent = data.t_cube.toFixed(1) + '°C';

            // Update mercury
            this.updateMercury(data.t_cube);

            // Update stability indicator
            this.updateStability(data.t_cube);

            // Update glow and heat based on temperature
            this.updateHeat(data.t_cube);
        }

        // Update timer (if available from backend)
        // This would need timer data

        // Update power level
        if (data.power !== undefined) {
            const powerText = document.querySelector('#hold-scheme #hold-power-pct');
            const powerBar = document.querySelector('#hold-scheme #hold-power-bar');

            if (powerText) powerText.textContent = Math.round(data.power) + '%';
            if (powerBar) powerBar.setAttribute('width', data.power);

            if (data.power > 0) {
                this.powerBtn?.classList.add('active');
            } else {
                this.powerBtn?.classList.remove('active');
            }
        }
    }

    updateMercury(temp) {
        const mercury = document.querySelector('#hold-scheme #hold-mercury');
        if (!mercury) return;

        // Map temperature to mercury height (50-100°C range)
        const minTemp = 50;
        const maxTemp = 100;
        const maxHeight = 530;

        const ratio = Math.min(Math.max((temp - minTemp) / (maxTemp - minTemp), 0), 1);
        const height = ratio * maxHeight;

        mercury.setAttribute('height', height);
        mercury.setAttribute('y', 680 - height);

        // Color based on deviation from target
        const deviation = Math.abs(temp - this.targetTemp);
        mercury.classList.remove('overheat', 'underheat');

        if (temp > this.targetTemp + 1.5) {
            mercury.classList.add('overheat');
        } else if (temp < this.targetTemp - 1.5) {
            mercury.classList.add('underheat');
        }
    }

    updateStability(temp) {
        const deviation = Math.abs(temp - this.targetTemp);
        const stabilityDot = document.querySelector('#hold-scheme #hold-stability-dot');
        const stabilityText = document.querySelector('#hold-scheme #hold-stability-text');

        if (!stabilityDot || !stabilityText) return;

        stabilityDot.classList.remove('excellent', 'normal', 'poor');

        if (deviation <= 0.5) {
            stabilityDot.classList.add('excellent');
            stabilityText.textContent = '🟢 Отлично';
        } else if (deviation <= 1.0) {
            stabilityDot.classList.add('normal');
            stabilityText.textContent = '🟡 Нормально';
        } else {
            stabilityDot.classList.add('poor');
            stabilityText.textContent = '🔴 Нестабильно';
        }
    }

    updateHeat(temp) {
        const glow = document.querySelector('#hold-scheme #hold-glow');
        const heat = document.querySelector('#hold-scheme #hold-heat');

        // Show glow/heat when temperature is being maintained
        if (temp > this.targetTemp - 2) {
            glow?.classList.add('active');
            heat?.classList.add('active');
        } else {
            glow?.classList.remove('active');
            heat?.classList.remove('active');
        }
    }
}

// ========== Global Instance ==========

let schemeController = null;

document.addEventListener('DOMContentLoaded', function() {
    // Check if scheme container exists
    const container = document.querySelector('.scheme-container');
    if (container) {
        schemeController = new SchemeController();
        console.log('Scheme controller initialized');
    }
});

// Function to update from main app.js
function updateColumnAnimation(data) {
    if (schemeController) {
        schemeController.updateState(data);
    }
}
