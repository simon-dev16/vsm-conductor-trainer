"""Server-only model adapter. No credentials or provider calls belong in Unreal."""



class ProviderError(Exception):
    def __init__(self, code, message):
        super().__init__(message)
        self.code = code


HTTP_REASONS={400:'bad_request',401:'unauthorized',403:'forbidden',404:'model_not_found',
              408:'timeout',429:'rate_limited'}

HTTP_HINTS={
    'bad_request':'Провайдер отклонил сам запрос. Проверьте URI модели и формат полей.',
    'unauthorized':'Ключ отклонён. Проверьте, что он создан, скопирован целиком и не отозван.',
    'forbidden':'Ключ рабочий, но не хватает прав. Проверьте папку и что модель доступна этому аккаунту.',
    'model_not_found':'Модель или папка не найдена. Проверьте VSM_YANDEX_MODEL_URI и VSM_YANDEX_FOLDER_ID, а также что доступ к модели открыт в кабинете.',
    'timeout':'Провайдер не успел ответить. Увеличьте VSM_AI_TIMEOUT.',
    'rate_limited':'Лимит запросов к модели исчерпан. Проверьте квоту в кабинете.',
    'unavailable':'Сбой на стороне провайдера. Запрос можно повторить.',
    'http_error':'Непредусмотренный ответ провайдера.',
}


def http_failure(status,prefix,subject='Провайдер'):
    """Разбирает HTTP-статус провайдера в код, по которому видно, что именно исправлять.

    Тело ответа не переносится: провайдер иногда возвращает в нём контекст запроса.
    """
    reason=HTTP_REASONS.get(status)
    if reason is None:
        reason='unavailable' if status>=500 else 'http_error'
    hint=HTTP_HINTS[reason]
    if reason in ('unavailable','http_error'):
        hint=f'{subject} вернул HTTP {status}. {hint}'
    return f'{prefix}_{reason}',hint
