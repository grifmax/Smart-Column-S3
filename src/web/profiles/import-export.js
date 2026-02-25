// Экспорт одного профиля

export function exportProfile(id) {

    fetch(`/api/profiles/${id}/export`)

        .then(response => response.json())

        .then(data => {

            // Создаем blob и скачиваем

            const blob = new Blob([JSON.stringify(data, null, 2)], { type: 'application/json' });

            const url = URL.createObjectURL(blob);

            const a = document.createElement('a');

            a.href = url;

            a.download = `profile_${data.metadata.name.replace(/\s+/g, '_')}_${id}.json`;

            document.body.appendChild(a);

            a.click();

            document.body.removeChild(a);

            URL.revokeObjectURL(url);

        })

        .catch(error => {

            console.error('Ошибка экспорта профиля:', error);

            alert('❌ Ошибка экспорта профиля');

        });

}



// Экспорт всех профилей

export function exportAllProfiles() {

    const includeBuiltin = confirm('Включить встроенные рецепты в экспорт?');



    fetch(`/api/profiles/export${includeBuiltin ? '?includeBuiltin=true' : ''}`)

        .then(response => response.json())

        .then(data => {

            if (!data || data.length === 0) {

                alert('Нет профилей для экспорта');

                return;

            }



            // Создаем blob и скачиваем

            const blob = new Blob([JSON.stringify(data, null, 2)], { type: 'application/json' });

            const url = URL.createObjectURL(blob);

            const a = document.createElement('a');

            a.href = url;

            const timestamp = new Date().toISOString().split('T')[0];

            a.download = `profiles_export_${timestamp}.json`;

            document.body.appendChild(a);

            a.click();

            document.body.removeChild(a);

            URL.revokeObjectURL(url);



            alert(`✅ Экспортировано профилей: ${data.length}`);

        })

        .catch(error => {

            console.error('Ошибка экспорта профилей:', error);

            alert('❌ Ошибка экспорта профилей');

        });

}



// Показать модальное окно импорта

export let importFileData = null;



export function showImportModal() {

    importFileData = null;

    document.getElementById('import-file-input').value = '';

    document.getElementById('import-preview').style.display = 'none';

    document.getElementById('import-btn').disabled = true;

    document.getElementById('profile-import-modal').style.display = 'flex';



    // Добавляем обработчик выбора файла

    document.getElementById('import-file-input').onchange = function (e) {

        const file = e.target.files[0];

        if (!file) return;



        const reader = new FileReader();

        reader.onload = function (event) {

            try {

                importFileData = JSON.parse(event.target.result);



                // Показываем предпросмотр

                let previewText = '';

                if (Array.isArray(importFileData)) {

                    previewText = `Массив из ${importFileData.length} профилей`;

                } else if (importFileData.metadata) {

                    previewText = `Профиль: ${importFileData.metadata.name}`;

                } else {

                    throw new Error('Неверный формат JSON');

                }



                document.getElementById('import-preview-text').textContent = previewText;

                document.getElementById('import-preview').style.display = 'block';

                document.getElementById('import-btn').disabled = false;

            } catch (error) {

                alert('❌ Ошибка чтения файла: неверный формат JSON');

                importFileData = null;

                document.getElementById('import-btn').disabled = true;

            }

        };

        reader.readAsText(file);

    };

}



// Закрыть модальное окно импорта

export function closeImportModal() {

    document.getElementById('profile-import-modal').style.display = 'none';

    importFileData = null;

}



// Выполнить импорт профилей

export function doImportProfiles() {

    if (!importFileData) {

        alert('Выберите файл для импорта');

        return;

    }



    fetch('/api/profiles/import', {

        method: 'POST',

        headers: { 'Content-Type': 'application/json' },

        body: JSON.stringify(importFileData)

    })

        .then(response => response.json())

        .then(data => {

            if (data.success) {

                closeImportModal();

                loadProfilesList();

                alert(`✅ Импортировано профилей: ${data.imported}`);

            } else {

                alert('❌ Ошибка импорта: ' + (data.error || 'Неизвестная ошибка'));

            }

        })

        .catch(error => {

            console.error('Ошибка импорта профилей:', error);

            alert('❌ Ошибка импорта профилей');

        });

}
