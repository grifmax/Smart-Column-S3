import { miniChart } from '../globals.js';
import { addLog } from './logs.js';

export function setTheme(theme) {

    document.body.setAttribute('data-theme', theme);

    localStorage.setItem('theme', theme);



    // Обновить тему мини-графика

    if (miniChart) {

        miniChart.updateOptions({

            theme: {

                mode: theme

            }

        });

    }



    addLog(`🌓 Тема изменена: ${theme}`, 'info');

}



export function loadTheme() {

    const savedTheme = localStorage.getItem('theme') || 'light';

    document.body.setAttribute('data-theme', savedTheme);

}
